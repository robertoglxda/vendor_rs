
/*
 * Copyright (C) 2018 The LineageOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#define LOG_TAG "ril-wrapper"
/*
 * We're using RIL_Env, which is only exposed if this is defined.
 */
#define RIL_SHLIB
#include <log/log.h>
#include <telephony/ril.h>
#include <dlfcn.h>
#define RIL_LIB_NAME "libril-qc-qmi-1.so"
/*
 * These structs are only avaiable in ril_internal.h
 * which is not exposed.
 */
typedef struct {
    int requestNumber;
    void (*dispatchFunction)(void* p, void* pRI);
    int (*responseFunction)(void* p, void* response, size_t responselen);
} CommandInfo;
typedef struct RequestInfo {
    int32_t token;
    CommandInfo* pCI;
    struct RequestInfo* p_next;
    char cancelled;
    char local;
} RequestInfo;
static const RIL_RadioFunctions* qmiRilFunctions;
static const struct RIL_Env* ossRilEnv;
static void onRequestCompleteShim(RIL_Token t, RIL_Errno e, void* response, size_t responselen) {
    if (!response) {
        ALOGV("%s: response is NULL", __func__);
        goto do_not_handle;
    }
    RequestInfo* requestInfo = (RequestInfo*)t;
    if (!requestInfo) {
        ALOGE("%s: request info is NULL", __func__);
        goto do_not_handle;
    }
    int request = requestInfo->pCI->requestNumber;
    switch (request) {
        /*
         * RIL can report a BASEBAND_VERSION bigger that 45 characters.
         * AOSP does not support this, so reduce the string lenght keeping
	 * the latest characters as they are the more significant. 
         */
	case RIL_REQUEST_BASEBAND_VERSION:
	    if (response == NULL) {
                ALOGE("%s: invalid response length", __func__);
                goto do_not_handle;
            }
	    char* tempResponse = (char*)response;
	    int m;
	    int charcount = 0;
	    const int maxlenght = 45;
            char finalResponse[maxlenght] = "";

	    // Check how long is the current BASEBAND_VERSION
	    for ( m = 0; tempResponse[m]; m++) {
		   if ( tempResponse[m] != ' ') {
			  charcount++;
		   }
	    }

	    // Remove the first characters to get to 'maxlenght' characters
	    int newcharcount = charcount - maxlenght;
	    if ( newcharcount > 0) {
		    for (  m = 0 ; m < maxlenght; m++) {
			  finalResponse[m] = tempResponse[newcharcount + m];
		    }
	    }
	    else goto do_not_handle;

	    // Replace the original 'response' with our new baseband
	    response = (void*)finalResponse;
	    break;
    }
do_not_handle:
    ossRilEnv->OnRequestComplete(t, e, response, responselen);
}
const RIL_RadioFunctions* RIL_Init(const struct RIL_Env* env, int argc, char** argv) {
    RIL_RadioFunctions const* (*qmiRilInit)(const struct RIL_Env* env, int argc, char** argv);
    static struct RIL_Env shimmedRilEnv;
    void* qmiRil;
    /*
     * Save the RilEnv passed from rild.
     */
    ossRilEnv = env;
    /*
     * Copy the RilEnv and shim the OnRequestComplete function.
     */
    shimmedRilEnv = *env;
    shimmedRilEnv.OnRequestComplete = onRequestCompleteShim;
    /*
     * Open the qmi RIL.
     */
    qmiRil = dlopen(RIL_LIB_NAME, RTLD_LOCAL);
    if (!qmiRil) {
        ALOGE("%s: failed to load %s: %s\n", __func__, RIL_LIB_NAME, dlerror());
        return NULL;
    }
    /*
     * Get a reference to the qmi RIL_Init.
     */
    qmiRilInit = dlsym(qmiRil, "RIL_Init");
    if (!qmiRilInit) {
        ALOGE("%s: failed to find RIL_Init\n", __func__);
        goto fail_after_dlopen;
    }
    ALOGE("I'm RIL");
    /*
     * Init the qmi RIL add pass it the shimmed RilEnv.
     */
    qmiRilFunctions = qmiRilInit(&shimmedRilEnv, argc, argv);
    if (!qmiRilFunctions) {
        ALOGE("%s: failed to get functions from RIL_Init\n", __func__);
        goto fail_after_dlopen;
    }
    return qmiRilFunctions;
fail_after_dlopen:
    dlclose(qmiRil);
    return NULL;
}

