/* Standard C functions used throughout. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdarg.h>

#include <jni.h>
#include <jvmti.h>
#include <string>

#define MAX_TOKEN_LENGTH        80
#define MAX_METHOD_NAME_LENGTH  256


#define AGENTNAME               "devops"
#define AGENTLIB                "-agentlib:" AGENTNAME


using namespace std;

void
error_exit_process(int exit_code)
{
    exit(exit_code);
}



/* Send message to stderr or whatever the error output location is and exit  */
void
fatal_error(const char *format, ...) {
    va_list ap;

    va_start(ap, format);
    (void) vfprintf(stderr, format, ap);
    (void) fflush(stderr);
    va_end(ap);
    exit(3);
}

/* Get a token from a string (strtok is not MT-safe)
 *    str       String to scan
 *    seps      Separation characters
 *    buf       Place to put results
 *    max       Size of buf
 *  Returns NULL if no token available or can't do the scan.
 */
char *
get_token(char *str, char *seps, char *buf, int max) {
    int len;

    buf[0] = 0;
    if (str == NULL || str[0] == 0) {
        return NULL;
    }
    str += strspn(str, seps);
    if (str[0] == 0) {
        return NULL;
    }
    len = (int) strcspn(str, seps);
    if (len >= max) {
        return NULL;
    }
    (void) strncpy(buf, str, len);
    buf[len] = 0;
    return str + len;
}


typedef struct {
    jvmtiEnv *jvmti;
    jrawMonitorID lock;
    char *packageName;
    char *filePath;
} GlobalAgentData;

static GlobalAgentData *gdata;

void SystemPrintln(JNIEnv *env,char* filePaths ,jobject exception) {
    //此处代码用java表示
    //e.printStackTrace(new PrintStream("filePath"));
    jclass throwable_class = env->FindClass("java/lang/Throwable");
    jclass printStreamClazz=env->FindClass("java/io/PrintStream");
    jmethodID printStreamConstructor=env->GetMethodID(printStreamClazz,"<init>","(Ljava/lang/String;)V");
    jobject filePath= env->NewStringUTF(filePaths);
    jobject printStreamInstances=env->NewObject(printStreamClazz, printStreamConstructor, reinterpret_cast<const jvalue *>(filePath));
    jmethodID print_method = env->GetMethodID(throwable_class, "printStackTrace", "(Ljava/io/PrintStream;)V");
    env->CallVoidMethod(exception, print_method, reinterpret_cast<const jvalue *>(printStreamInstances));

}


static void
print_usage(void) {

    (void) fprintf(stdout,
                   "\n"
                   "Devops: 找异常代码神器 \n"
                   "项目无法成功启动怎么办？项目启动成功后报的异常无厘头怎么办？自己写的异常被吞了怎么办？框架层面的异常打印不出来怎么办？\n"
                   "注意：本工具不可以使用在生产环境，后果自负 \n"
                   "\n"
                   AGENTNAME " usage: java " AGENTLIB "=[help]|[<option>=<value>, ...]\n"
                   "\n"
                   "Option Name and Value  Description                                                  Default\n"
                   "---------------------  -----------------------------------------                    -------\n"
                   "packageName=samples    指定的包，com.pingan.ide.aicsp.xx.controller                     all\n"
                   "filePath=samplesFilePath 指定路径名称，用于写入异常，比如'/wls81/user/ssss/'，捕捉到异常后会在这里写入  "

    );
}



static void parse_agent_options(char *options) {




    char token[MAX_TOKEN_LENGTH];
    char *next;

    printf("options {}",options);
    /* Parse options and set flags in gdata */
    if (options == NULL) {
        print_usage();
        error_exit_process(0);
        return;
    }

    if (options == 0)
        options = "";

    if ((strcmp(options, "help")) == 0) {
        print_usage();
        error_exit_process(0);
    }

    next = get_token(options, ",=", token, sizeof(token));

    /* While not at the end of the options string, process this option. */
    while (next != NULL) {
        if (strcmp(token, "packageName") == 0) {
            int used;
            int maxlen;

            maxlen = MAX_METHOD_NAME_LENGTH;
            if (gdata->packageName == NULL) {
                gdata->packageName = (char *) calloc(maxlen + 1, 1);
                used = 0;
            } else {
                used = (int) strlen(gdata->packageName);
                gdata->packageName[used++] = ',';
                gdata->packageName[used] = 0;
                gdata->packageName = (char *)
                        realloc((void *) gdata->packageName, used + maxlen + 1);
            }
            if (gdata->packageName == NULL) {
                break;
            }
            /* Add this item to the list */
            next = get_token(next, ",=", gdata->packageName + used, maxlen);
            /* Check for token scan error */
            if (next == NULL) {
                fatal_error("ERROR: include option error\n");
            }
        }else if(strcmp(token, "filePath") == 0){
            int used;
            int maxlen;

            maxlen = MAX_METHOD_NAME_LENGTH;
            if (gdata->filePath == NULL) {
                gdata->filePath = (char *) calloc(maxlen + 1, 1);
                used = 0;
            } else {
                used = (int) strlen(gdata->filePath);
                gdata->filePath[used++] = ',';
                gdata->filePath[used] = 0;
                gdata->filePath = (char *)
                        realloc((void *) gdata->filePath, used + maxlen + 1);
            }
            if (gdata->filePath == NULL) {
                break;
            }
            /* Add this item to the list */
            next = get_token(next, ",=", gdata->filePath + used, maxlen);
            /* Check for token scan error */
            if (next == NULL) {
                fatal_error("ERROR: include option error\n");
            }
        }

        else if (token[0] != 0) {
            /* We got a non-empty token and we don't know what it is. */
            fatal_error("ERROR: Unknown option: %s\n", token);
        }
        /* Get the next token (returns NULL if there are no more) */
        next = get_token(next, ",=", token, sizeof(token));
    }
}

static bool check_jvmti_error(jvmtiEnv *jvmti, jvmtiError errnum,
                              const char *str) {
    if (errnum != JVMTI_ERROR_NONE) {
        char *errnum_str;
        errnum_str = NULL;
        (void) (jvmti->GetErrorName(errnum, &errnum_str));
        fatal_error("ERROR: JVMTI: %d(%s): %s\n", errnum,(errnum_str == NULL ? "Unknown" : errnum_str),(str == NULL ? "" : str));
        return false;
    }
    return true;
}

static void JNICALL callbackException(jvmtiEnv *jvmti, JNIEnv *env,
                                      jthread thread, jmethodID method, jlocation location, jobject exception,
                                      jmethodID catch_method, jlocation catch_location) {
    // 获得方法对应的类
    jclass clazz;
    jvmti->GetMethodDeclaringClass(method, &clazz);
    // 获得类的签名
    char *class_signature;
    jvmti->GetClassSignature(clazz, &class_signature, nullptr);

    if (!gdata->packageName == NULL) {
        //过滤非本工程类信息
        std::string::size_type idx;
        std::string class_signature_str = class_signature;
        idx = class_signature_str.find(string(gdata->packageName));
        if (idx != 1) {
            return;
        }
    }
    if (gdata->filePath == NULL) {
        printf("filePath不能为空\n");
        error_exit_process(1);
    }
    SystemPrintln(env, gdata->filePath,exception);
}


JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *jvm, char *options, void *reserved) {
    jvmtiEnv *jvmti;
    jvmtiCapabilities capabilities;
    jvmtiError error;
    jint result;
    jvmtiEventCallbacks callbacks;

    result = jvm->GetEnv((void **) &jvmti, JVMTI_VERSION);
    if (result != JNI_OK) {
        printf("Unable to access JVMTI! \n");
    }

    gdata = (GlobalAgentData *) malloc(sizeof(GlobalAgentData));
    gdata->jvmti = jvmti;
    parse_agent_options(options);
    (void) memset(&capabilities, 0, sizeof(jvmtiCapabilities));
    capabilities.can_generate_exception_events = 1;
    capabilities.can_access_local_variables = 1;
    capabilities.can_get_source_file_name = 1;
    capabilities.can_get_line_numbers = 1;
    capabilities.can_get_synthetic_attribute = 1;
    capabilities.can_get_bytecodes = 1;
    capabilities.can_tag_objects = 1;
    capabilities.can_pop_frame=1;
    error = (*(gdata->jvmti)).AddCapabilities(&capabilities);
    check_jvmti_error(gdata->jvmti, error, "Unable to set Capabilities");

    error = jvmti->SetEventNotificationMode(JVMTI_ENABLE,
                                            JVMTI_EVENT_EXCEPTION, (jthread) NULL);
    check_jvmti_error(jvmti, error, "Cannot set Exception Event notification");

    (void) memset(&callbacks, 0, sizeof(callbacks));
    callbacks.Exception = &callbackException;

    error = (*(gdata->jvmti)).SetEventCallbacks(&callbacks,
                                                (jint) sizeof(callbacks));
    check_jvmti_error(gdata->jvmti, error, "Cannot set event callbacks");

    error = (*(gdata->jvmti)).CreateRawMonitor("agent data", &(gdata->lock));
    check_jvmti_error(gdata->jvmti, error, "Cannot create raw monitor");

    printf("异常日志捕捉神器启动成功!!\n");
    return JNI_OK;
}