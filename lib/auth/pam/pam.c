/*
 * Copyright (c) 1995, 1996 Kungliga Tekniska H�gskolan (Royal Institute
 * of Technology, Stockholm, Sweden).
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the Kungliga Tekniska
 *      H�gskolan and its contributors.
 * 
 * 4. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* This code is extremely ugly, and would probably be better off
   beeing completely rewritten */


#ifdef HAVE_CONFIG_H
#include<config.h>
RCSID("$Id$");
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <unistd.h>

#define PAM_SM_AUTH
#define PAM_SM_SESSION
#include <security/pam_modules.h>

#include <netinet/in.h>
#include <krb.h>
#include <kafs.h>

static int
cleanup(pam_handle_t *pamh, void *data, int error_code)
{
    if(error_code != PAM_SUCCESS)
	dest_tkt();
    free(data);
    return PAM_SUCCESS;
}

static int
doit(pam_handle_t *pamh, char *name, char *inst, char *pwd, char *tkt)
{
    char realm[REALM_SZ];
    int ret;

    pam_set_data(pamh, "KRBTKFILE", strdup(tkt), cleanup);
    krb_set_tkt_string(tkt);
	
    krb_get_lrealm(realm, 1);
    ret = krb_verify_user(name, inst, realm, pwd, 1, NULL);
    memset(pwd, 0, strlen(pwd));
    switch(ret){
    case KSUCCESS:
	return PAM_SUCCESS;
    case KDC_PR_UNKNOWN:
	return PAM_USER_UNKNOWN;
    case SKDC_CANT:
    case SKDC_RETRY:
    case RD_AP_TIME:
	return PAM_AUTHINFO_UNAVAIL;
    default:
	return PAM_AUTH_ERR;
    }
}

static int
auth_login(pam_handle_t *pamh, int flags, char *user, struct pam_conv *conv)
{
    int ret;
    struct pam_message msg, *pmsg;
    struct pam_response *resp;
    char prompt[128];

    pmsg = &msg;
    msg.msg_style = PAM_PROMPT_ECHO_OFF;
    sprintf(prompt, "%s's Password: ", user);
    msg.msg = prompt;

    ret = conv->conv(1, (const struct pam_message**)&pmsg, 
		     &resp, conv->appdata_ptr);
    if(ret != PAM_SUCCESS)
	return ret;
    
    {
	char tkt[1024];
	struct passwd *pw = getpwnam(user);
	if(pw){
	    sprintf(tkt, "%s%d", TKT_ROOT, pw->pw_uid);
	    ret = doit(pamh, user, "", resp->resp, tkt);
	    if(ret == PAM_SUCCESS)
		chown(tkt, pw->pw_uid, pw->pw_uid);
	}else
	    ret = PAM_USER_UNKNOWN;
	memset(resp->resp, 0, strlen(resp->resp));
	free(resp->resp);
	free(resp);
    }
    return ret;
}

static int
auth_su(pam_handle_t *pamh, int flags, char *user, struct pam_conv *conv)
{
    int ret;
    struct passwd *pw;
    struct pam_message msg, *pmsg;
    struct pam_response *resp;
    char prompt[128];
    krb_principal pr;
    
    pr.realm[0] = 0;
    ret = pam_get_user(pamh, &user, "login: ");
    if(ret != PAM_SUCCESS)
	return ret;
    
    pw = getpwuid(getuid());
    if(strcmp(user, "root") == 0){
	strcpy(pr.name, pw->pw_name);
	strcpy(pr.instance, "root");
    }else{
	strcpy(pr.name, user);
	pr.instance[0] = 0;
    }
    pmsg = &msg;
    msg.msg_style = PAM_PROMPT_ECHO_OFF;
    sprintf(prompt, "%s's Password: ", krb_unparse_name(&pr));
    msg.msg = prompt;

    ret = conv->conv(1, (const struct pam_message**)&pmsg, 
		     &resp, conv->appdata_ptr);
    if(ret != PAM_SUCCESS)
	return ret;
    
    {
	char tkt[1024];
	sprintf(tkt, "%s_%s_to_%s", TKT_ROOT, pw->pw_name, user);
	ret = doit(pamh, pr.name, pr.inst, resp->resp, tkt);
	if(ret == PAM_SUCCESS)
	    chown(tkt, pw->pw_uid, pw->pw_uid);
	memset(resp->resp, 0, strlen(resp->resp));
	free(resp->resp);
	free(resp);
    }
    return ret;
}

int
pam_sm_authenticate(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
    char *user;
    int ret;
    struct pam_conv *conv;
    ret = pam_get_user(pamh, &user, "login: ");
    if(ret != PAM_SUCCESS)
	return ret;

    ret = pam_get_item(pamh, PAM_CONV, (void*)&conv);
    if(ret != PAM_SUCCESS)
	return ret;

    
    if(getuid() != geteuid())
	return auth_su(pamh, flags, user, conv);
    else
	return auth_login(pamh, flags, user, conv);
}

int 
pam_sm_setcred(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
    return PAM_SUCCESS;
}


int
pam_sm_open_session(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
    char *tkt;
    pam_get_data(pamh, "KRBTKFILE", (const void**)&tkt);
    setenv("KRBTKFILE", tkt, 1);
    if(k_hasafs()){
	k_setpag();
	k_afsklog(0, 0);
    }
    return PAM_SUCCESS;
}


int
pam_sm_close_session(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
    dest_tkt();
    if(k_hasafs())
	k_unlog();
    return PAM_SUCCESS;
}
