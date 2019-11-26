#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <libHX/string.h>
#include "util.h"
#include "guid.h"
#include "oxcmail.h"
#include "str_hash.h"
#include "rop_util.h"
#include "list_file.h"
#include "config_file.h"
#include "cache_queue.h"
#include "exmdb_local.h"
#include "net_failure.h"
#include "exmdb_client.h"
#include "bounce_audit.h"
#include "auto_response.h"
#include "alloc_context.h"
#include "tpropval_array.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>


#define MAX_DIGLEN				256*1024

#define DEF_MODE				S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH

static char g_org_name[256];
static char g_config_path[256];
static char g_propname_path[256];
static pthread_key_t g_alloc_key;
static STR_HASH_TABLE *g_str_hash;
static char g_default_charset[32];
static char g_default_timezone[64];
static pthread_mutex_t g_sequence_lock;

BOOL (*exmdb_local_check_domain)(const char *domainname);

static BOOL (*exmdb_local_get_user_info)(const char *username,
	char *home_dir, char *charset, char *timezone);

SPAM_STATISTIC exmdb_local_spam_statistic;

BOOL (*exmdb_local_get_lang)(const char *username, char *lang);

BOOL (*exmdb_local_get_timezone)(const char *username, char *timezone);

BOOL (*exmdb_local_check_same_org2)(
	const char *domainname1, const char *domainname2);

BOOL (*exmdb_local_lang_to_charset)(const char *lang, char *charset);

static uint32_t (*exmdb_local_ltag_to_lcid)(const char*);

static const char* (*exmdb_local_lcid_to_ltag)(uint32_t);

static uint32_t (*exmdb_local_charset_to_cpid)(const char*);

static const char* (*exmdb_local_cpid_to_charset)(uint32_t);

static const char* (*exmdb_local_mime_to_extension)(const char*);

static const char* (*exmdb_local_extension_to_mime)(const char*);

static BOOL (*exmdb_local_get_user_ids)(const char*, int*, int*, int*);

static BOOL (*exmdb_local_get_username)(int, char*);


static int exmdb_local_sequence_ID()
{
	int temp_ID;
	static int sequence_ID = 1;
	
	pthread_mutex_lock(&g_sequence_lock);
	if (sequence_ID >= 0X7FFFFFFF) {
		sequence_ID = 1;
	} else {
		sequence_ID ++;
	}
	temp_ID = sequence_ID;
	pthread_mutex_unlock(&g_sequence_lock);
	return temp_ID;
}


void exmdb_local_init(const char *config_path,
	const char *org_name, const char *default_charset,
	const char *default_timezone, const char *propname_path)
{
	strcpy(g_config_path, config_path);
	strcpy(g_org_name, org_name);
	strcpy(g_default_charset, default_charset);
	strcpy(g_default_timezone, default_timezone);
	strcpy(g_propname_path, propname_path);
	pthread_key_create(&g_alloc_key, NULL);
	pthread_mutex_init(&g_sequence_lock, NULL);
}

int exmdb_local_run()
{
	int i, num;
	char *pitem;
	int last_propid;
	LIST_FILE *plist;
	char temp_line[256];
	
	exmdb_local_check_domain = query_service("check_domain");
	if (NULL == exmdb_local_check_domain) {
		printf("[exmdb_local]: fail to get \"check_domain\" service\n");
		return -1;
	}
	exmdb_local_get_user_info = query_service("get_user_info");
	if (NULL == exmdb_local_get_user_info) {
		printf("[exmdb_local]: fail to get \"get_user_info\" service\n");
		return -1;
	}
	exmdb_local_spam_statistic = query_service("spam_statistic");
	
	exmdb_local_get_lang = query_service("get_user_lang");
	if (NULL == exmdb_local_get_lang) {
		printf("[exmdb_local]: fail to get \"get_user_lang\" service\n");
		return -1;
	}
	exmdb_local_get_timezone = query_service("get_user_timezone");
	if (NULL == exmdb_local_get_timezone) {
		printf("[exmdb_local]: fail to get \"get_user_timezone\" service\n");
		return -1;
	}
	exmdb_local_check_same_org2 = query_service("check_same_org2");
	if (NULL == exmdb_local_check_same_org2) {
		printf("[exmdb_local]: fail to get \"check_same_org2\" service\n");
		return -1;
	}
	exmdb_local_lang_to_charset = query_service("lang_to_charset");
	if (NULL == exmdb_local_lang_to_charset) {
		printf("[exmdb_local]: fail to get \"lang_to_charset\" service\n");
		return -1;
	}
	exmdb_local_ltag_to_lcid = query_service("ltag_to_lcid");
	if (NULL == exmdb_local_ltag_to_lcid) {
		printf("[exmdb_local]: fail to get \"ltag_to_lcid\" service\n");
		return -1;
	}
	exmdb_local_lcid_to_ltag = query_service("lcid_to_ltag");
	if (NULL == exmdb_local_ltag_to_lcid) {
		printf("[exmdb_local]: fail to get \"lcid_to_ltag\" service\n");
		return -1;
	}
	exmdb_local_charset_to_cpid = query_service("charset_to_cpid");
	if (NULL == exmdb_local_charset_to_cpid) {
		printf("[exmdb_local]: fail to get \"charset_to_cpid\" service\n");
		return -1;
	}
	exmdb_local_cpid_to_charset = query_service("cpid_to_charset");
	if (NULL == exmdb_local_cpid_to_charset) {
		printf("[exmdb_local]: fail to get \"cpid_to_charset\" service\n");
		return -1;
	}
	exmdb_local_mime_to_extension = query_service("mime_to_extension");
	if (NULL == exmdb_local_mime_to_extension) {
		printf("[exmdb_local]: fail to get \"mime_to_extension\" service\n");
		return -1;
	}
	exmdb_local_extension_to_mime = query_service("extension_to_mime");
	if (NULL == exmdb_local_extension_to_mime) {
		printf("[exmdb_local]: fail to get \"extension_to_mime\" service\n");
		return -1;
	}
	exmdb_local_get_user_ids = query_service("get_user_ids");
	if (NULL == exmdb_local_get_user_ids) {
		printf("[exmdb_local]: fail to get \"get_user_ids\" service\n");
		return -1;
	}
	exmdb_local_get_username = query_service("get_username");
	if (NULL == exmdb_local_get_username) {
		printf("[exmdb_local]: fail to get \"get_username\" service\n");
		return -1;
	}
	if (FALSE == oxcmail_init_library(g_org_name,
		exmdb_local_get_user_ids, exmdb_local_get_username,
		exmdb_local_ltag_to_lcid, exmdb_local_lcid_to_ltag,
		exmdb_local_charset_to_cpid, exmdb_local_cpid_to_charset,
		exmdb_local_mime_to_extension, exmdb_local_extension_to_mime)) {
		printf("[exmdb_local]: fail to init oxcmail library\n");
		return -2;
	}
	plist = list_file_init(g_propname_path, "%s:256");
	if (NULL == plist) {
		printf("[exmdb_local]: Failed to read property name list from %s: %s\n",
			g_propname_path, strerror(errno));
		return -3;
	}
	num = list_file_get_item_num(plist);
	pitem = list_file_get_list(plist);
	g_str_hash = str_hash_init(num + 1, sizeof(uint16_t), NULL);
	if (NULL == g_str_hash) {
		printf("[exmdb_local]: fail to init hash table\n");
		return -4;
	}
	last_propid = 0x8001;
	for (i=0; i<num; i++) {
		strcpy(temp_line, pitem + 256*i);
		HX_strlower(temp_line);
		str_hash_add(g_str_hash, temp_line, &last_propid);
		last_propid ++;
	}
	return 0;
}

int exmdb_local_stop()
{
	if (NULL != g_str_hash) {
		str_hash_free(g_str_hash);
		g_str_hash = NULL;
	}
	return 0;
}

void exmdb_local_free()
{
	pthread_mutex_destroy(&g_sequence_lock);
	pthread_key_delete(g_alloc_key);
}

BOOL exmdb_local_hook(MESSAGE_CONTEXT *pcontext)
{
	int cache_ID;
	char *pdomain;
	BOOL remote_found;
	char rcpt_buff[256];
	time_t current_time;
	MEM_FILE remote_file;
	MESSAGE_CONTEXT *pbounce_context;
	
	remote_found = FALSE;
	if (BOUND_NOTLOCAL == pcontext->pcontrol->bound_type) {
		return FALSE;
	}
	mem_file_init(&remote_file, pcontext->pcontrol->f_rcpt_to.allocator);
	while (MEM_END_OF_FILE != mem_file_readline(
		&pcontext->pcontrol->f_rcpt_to, rcpt_buff, 256)) {
		pdomain = strchr(rcpt_buff, '@');
		if (NULL == pdomain) {
			mem_file_writeline(&remote_file, rcpt_buff);
			continue;
		}
		pdomain ++;
		if (TRUE == exmdb_local_check_domain(pdomain)) {
			switch (exmdb_local_deliverquota(pcontext, rcpt_buff)) {
			case DELIVERY_OPERATION_OK:
				net_failure_statistic(1, 0, 0, 0);
				if (NULL != exmdb_local_spam_statistic) {
					exmdb_local_spam_statistic(SPAM_STATISTIC_OK);
				}
				break;
			case DELIVERY_OPERATION_DELIVERED:
				net_failure_statistic(1, 0, 0, 0);
				if (NULL != exmdb_local_spam_statistic) {
					exmdb_local_spam_statistic(SPAM_STATISTIC_OK);
				}
				if (TRUE == pcontext->pcontrol->need_bounce &&
					0 != strcasecmp(pcontext->pcontrol->from, "none@none")) {
					pbounce_context = get_context();
					if (NULL == pbounce_context) {
						exmdb_local_log_info(pcontext, rcpt_buff, 8,
							"fail to get bounce context");
					} else {
						if (FALSE == bounce_audit_check(rcpt_buff)) {
							exmdb_local_log_info(pcontext, rcpt_buff, 8,
								"will not produce bounce message, "
								"because of too many mails to %s", rcpt_buff);
							put_context(pbounce_context);
						} else {
							time(&current_time);
							bounce_producer_make(pcontext->pcontrol->from,
								rcpt_buff, pcontext->pmail, current_time,
								BOUNCE_MAIL_DELIVERED, pbounce_context->pmail);
							pbounce_context->pcontrol->need_bounce = FALSE;
							sprintf(pbounce_context->pcontrol->from,
								"postmaster@%s", get_default_domain());
							mem_file_writeline(
								&pbounce_context->pcontrol->f_rcpt_to,
								pcontext->pcontrol->from);
							enqueue_context(pbounce_context);
						}
					}
				}
				break;
			case DELIVERY_NO_USER:
				net_failure_statistic(0, 0, 0, 1);
				if (TRUE == pcontext->pcontrol->need_bounce &&
					0 != strcasecmp(pcontext->pcontrol->from, "none@none")) {
					pbounce_context = get_context();
					if (NULL == pbounce_context) {
						exmdb_local_log_info(pcontext, rcpt_buff, 8,
							"fail to get bounce context");
					} else {
						if (FALSE == bounce_audit_check(rcpt_buff)) {
							exmdb_local_log_info(pcontext, rcpt_buff, 8,
								"will not produce bounce message, "
								"because of too many mails to %s", rcpt_buff);
							put_context(pbounce_context);
						} else {
							time(&current_time);
							bounce_producer_make(pcontext->pcontrol->from,
								rcpt_buff, pcontext->pmail, current_time,
								BOUNCE_NO_USER, pbounce_context->pmail);
							pbounce_context->pcontrol->need_bounce = FALSE;
							sprintf(pbounce_context->pcontrol->from,
								"postmaster@%s", get_default_domain());
							mem_file_writeline(
								&pbounce_context->pcontrol->f_rcpt_to,
								pcontext->pcontrol->from);
							enqueue_context(pbounce_context);
						}
					}
				}
				if (NULL != exmdb_local_spam_statistic) {
					exmdb_local_spam_statistic(SPAM_STATISTIC_NOUSER);
				}
				break;
			case DELIVERY_MAILBOX_FULL:
				if (TRUE == pcontext->pcontrol->need_bounce &&
					0 != strcasecmp(pcontext->pcontrol->from, "none@none")) {
					pbounce_context = get_context();
					if (NULL == pbounce_context) {
						exmdb_local_log_info(pcontext, rcpt_buff, 8,
							"fail to get bounce context");
					} else {
						if (FALSE == bounce_audit_check(rcpt_buff)) {
							exmdb_local_log_info(pcontext, rcpt_buff, 8,
								"will not produce bounce message, "
								"because of too many mails to %s", rcpt_buff);
							put_context(pbounce_context);
						} else {
							time(&current_time);
							bounce_producer_make(pcontext->pcontrol->from,
								rcpt_buff, pcontext->pmail, current_time,
								BOUNCE_MAILBOX_FULL, pbounce_context->pmail);
							pbounce_context->pcontrol->need_bounce = FALSE;
							sprintf(pbounce_context->pcontrol->from,
								"postmaster@%s", get_default_domain());
							mem_file_writeline(
								&pbounce_context->pcontrol->f_rcpt_to,
								pcontext->pcontrol->from);
							enqueue_context(pbounce_context);
						}
					}
				}
				break;
			case DELIVERY_OPERATION_ERROR:
				net_failure_statistic(0, 0, 1, 0);
				if (TRUE == pcontext->pcontrol->need_bounce &&
					0 != strcasecmp(pcontext->pcontrol->from, "none@none")) {
					pbounce_context = get_context();
					if (NULL == pbounce_context) {
						exmdb_local_log_info(pcontext, rcpt_buff, 8,
							"fail to get bounce context");
					} else {
						if (FALSE == bounce_audit_check(rcpt_buff)) {
							exmdb_local_log_info(pcontext, rcpt_buff, 8,
								"will not produce bounce message, "
								"because of too many mails to %s", rcpt_buff);
							put_context(pbounce_context);
						} else {
							time(&current_time);
							bounce_producer_make(pcontext->pcontrol->from,
								rcpt_buff, pcontext->pmail, current_time,
								BOUNCE_OPERATION_ERROR, pbounce_context->pmail);
							pbounce_context->pcontrol->need_bounce = FALSE;
							sprintf(pbounce_context->pcontrol->from,
								"postmaster@%s", get_default_domain());
							mem_file_writeline(
								&pbounce_context->pcontrol->f_rcpt_to,
								pcontext->pcontrol->from);
							enqueue_context(pbounce_context);
						}
					}
				}
				break;
			case DELIVERY_OPERATION_FAILURE:
				net_failure_statistic(0, 1, 0, 0);
				time(&current_time);
				cache_ID = cache_queue_put(pcontext, rcpt_buff, current_time);
				if (cache_ID >= 0) {
					exmdb_local_log_info(pcontext, rcpt_buff, 8,
						"message is put into cache queue with cache ID %d and "
						"wait to be delivered next time", cache_ID);
				} else {
					exmdb_local_log_info(pcontext, rcpt_buff, 8,
						"fail to put message into cache queue!!!");
				}
				break;
			}
		} else {
			remote_found = TRUE;
			mem_file_writeline(&remote_file, rcpt_buff);
		}
	}
	if (TRUE == remote_found) {
		mem_file_copy(&remote_file, &pcontext->pcontrol->f_rcpt_to);
		mem_file_free(&remote_file);
		return FALSE;
	} else {
		mem_file_free(&remote_file);
		return TRUE;
	}
}

static void* exmdb_local_alloc(size_t size)
{
	ALLOC_CONTEXT *pctx;
	
	pctx = pthread_getspecific(g_alloc_key);
	if (NULL == pctx) {
		return NULL;
	}
	return alloc_context_alloc(pctx, size);
}

BOOL exmdb_local_get_propids(
	const PROPNAME_ARRAY *ppropnames,
	PROPID_ARRAY *ppropids)
{
	int i;
	uint16_t *ppropid;
	char tmp_guid[64];
	char tmp_string[256];
	
	ppropids->count = ppropnames->count;
	ppropids->ppropid = exmdb_local_alloc(
		sizeof(uint16_t)*ppropnames->count);
	for (i=0; i<ppropnames->count; i++) {
		guid_to_string(&ppropnames->ppropname[i].guid, tmp_guid, 64);
		if (KIND_LID == ppropnames->ppropname[i].kind) {
			snprintf(tmp_string, 256, "GUID=%s,LID=%u",
				tmp_guid, *ppropnames->ppropname[i].plid);
		} else {
			snprintf(tmp_string, 256, "GUID=%s,NAME=%s",
				tmp_guid, ppropnames->ppropname[i].pname);
		}
		HX_strlower(tmp_string);
		ppropid = str_hash_query(g_str_hash, tmp_string);
		if (NULL == ppropid) {
			ppropids->ppropid[i] = 0;
		} else {
			ppropids->ppropid[i] = *ppropid;
		}
	}
	return TRUE;
}


int exmdb_local_deliverquota(MESSAGE_CONTEXT *pcontext, const char *address)
{
	int fd;
	int result;
	MAIL *pmail;
	int tmp_len;
	void *pvalue;
	char lang[32];
	size_t mess_len;
	int sequence_ID;
	time_t cur_time;
	uint64_t nt_time;
	char charset[32];
	char timezone[64];
	char hostname[128];
	char home_dir[256];
	uint32_t tmp_int32;
	char file_name[128];
	char temp_path[256];
	MESSAGE_CONTENT *pmsg;
	TAGGED_PROPVAL propval;
	uint32_t suppress_mask;
	BOOL b_bounce_delivered;
	ALLOC_CONTEXT alloc_ctx;
	char temp_buff[MAX_DIGLEN];
	MESSAGE_CONTEXT *pcontext1;

	
	if (FALSE == exmdb_local_get_user_info(
		address, home_dir, lang, timezone)) {
		exmdb_local_log_info(pcontext, address, 8, "fail"
			"to get user information from data source!");
		return DELIVERY_OPERATION_FAILURE;
	}
	if ('\0' == lang[0] || FALSE ==
		exmdb_local_lang_to_charset(lang,
		charset) || '\0' == charset[0]) {
		strcpy(charset, g_default_charset);
	}
	if ('\0' == home_dir[0]) {
		exmdb_local_log_info(pcontext, address, 8,
			"there's no user in mail system");
		return DELIVERY_NO_USER;
	}
	if ('\0' == timezone[0]) {
		strcpy(timezone, g_default_timezone);
	}
	
	pmail = pcontext->pmail;
	if (TRUE == mail_check_dot(pcontext->pmail)) {
		pcontext1 = get_context();
		if (NULL != pcontext1) {
			if (TRUE == mail_transfer_dot(
				pcontext->pmail, pcontext1->pmail)) {
				pmail = pcontext1->pmail;
			} else {
				put_context(pcontext1);
				pcontext1 = NULL;
			}
		}
	} else {
		pcontext1 = NULL;
	}
	
	time(&cur_time);
	sequence_ID = exmdb_local_sequence_ID();
	strncpy(hostname, get_host_ID(), 127);
	if ('\0' == hostname[0]) {
		if (gethostname(hostname, 127) < 0) {
			strcpy(hostname, "localhost");
		}
	}
	snprintf(file_name, 128, "%ld.%d.%s",
		cur_time, sequence_ID, hostname);
	snprintf(temp_path, 255, "%s/eml/%s", home_dir, file_name);
	fd = open(temp_path, O_CREAT|O_RDWR|O_TRUNC, DEF_MODE);
	if (-1 == fd) {
		if (NULL != pcontext1) {
			put_context(pcontext1);
		}
		exmdb_local_log_info(pcontext, address,
			8, "fail to creating mail file in"
			" directory %s/eml", home_dir);
		return DELIVERY_OPERATION_FAILURE;
	}
	
	if (FALSE == mail_to_file(pmail, fd)) {
		close(fd);
		remove(temp_path);
		if (NULL != pcontext1) {
			put_context(pcontext1);
		}
		exmdb_local_log_info(pcontext, address,
			8, "fail to write mail file in"
			" directory %s/eml", home_dir);
		return DELIVERY_OPERATION_FAILURE;
	}
	close(fd);

	tmp_len = sprintf(temp_buff, "{\"file\":\"%s\",", file_name);
	result = mail_get_digest(pmail, &mess_len, temp_buff + tmp_len,
				MAX_DIGLEN - tmp_len - 1);
	
	if (result <= 0) {
		remove(temp_path);
		if (NULL != pcontext1) {
			put_context(pcontext1);
		}
		exmdb_local_log_info(pcontext, address, 8,
			"permanent failure of getting mail digest!");
		return DELIVERY_OPERATION_ERROR;
	}
	tmp_len = strlen(temp_buff);
	temp_buff[tmp_len] = '}';
	tmp_len ++;
	temp_buff[tmp_len] = '\0';
	
	alloc_context_init(&alloc_ctx);
	pthread_setspecific(g_alloc_key, &alloc_ctx);
	pmsg = oxcmail_import(charset, timezone, pmail,
		exmdb_local_alloc, exmdb_local_get_propids);
	if (NULL != pcontext1) {
		put_context(pcontext1);
	}
	if (NULL == pmsg) {
		alloc_context_free(&alloc_ctx);
		pthread_setspecific(g_alloc_key, NULL);
		remove(temp_path);
		exmdb_local_log_info(pcontext, address, 8, "fail "
			"to convert rtf822 into MAPI message object");
		return DELIVERY_OPERATION_ERROR;
	}
	alloc_context_free(&alloc_ctx);
	pthread_setspecific(g_alloc_key, NULL);
	
	nt_time = rop_util_current_nttime();
	propval.proptag = PROP_TAG_MESSAGEDELIVERYTIME;
	propval.pvalue = &nt_time;
	tpropval_array_set_propval(&pmsg->proplist, &propval);
	
	if (FALSE == pcontext->pcontrol->need_bounce) {
		propval.proptag = PROP_TAG_AUTORESPONSESUPPRESS;
		propval.pvalue = &tmp_int32;
		tmp_int32 = 0xFFFFFFFF;
		tpropval_array_set_propval(
			&pmsg->proplist, &propval);
	}
	
	tpropval_array_remove_propval(
		&pmsg->proplist, PROP_TAG_CHANGENUMBER);
	result = exmdb_client_delivery_message(
		home_dir, pcontext->pcontrol->from,
		address, 0, pmsg, temp_buff);
	if (EXMDB_RESULT_OK == result) {
		pvalue = tpropval_array_get_propval(&pmsg->proplist,
							PROP_TAG_AUTORESPONSESUPPRESS);
		if (NULL == pvalue) {
			suppress_mask = 0;
		} else {
			suppress_mask = *(uint32_t*)pvalue;
		}
		pvalue = tpropval_array_get_propval(&pmsg->proplist,
				PROP_TAG_ORIGINATORDELIVERYREPORTREQUESTED);
		if (NULL != pvalue && 0 != *(uint8_t*)pvalue) {
			b_bounce_delivered = TRUE;
			if (suppress_mask & AUTO_RESPONSE_SUPPRESS_DR) {
				b_bounce_delivered = FALSE;
			}
		} else {
			b_bounce_delivered = FALSE;
		}
	}
	message_content_free(pmsg);
	switch (result) {
	case EXMDB_RESULT_OK:
		exmdb_local_log_info(pcontext, address, 8,
			"message %s is delivered OK", temp_path);
		if (TRUE == pcontext->pcontrol->need_bounce &&
			0 != strcmp(pcontext->pcontrol->from, "none@none") &&
			0 == (suppress_mask & AUTO_RESPONSE_SUPPRESS_OOF)) {
			auto_response_reply(home_dir, address, pcontext->pcontrol->from);
		}
		if (TRUE == b_bounce_delivered) {
			return DELIVERY_OPERATION_DELIVERED;
		}
		return DELIVERY_OPERATION_OK;
	case EXMDB_RUNTIME_ERROR:
		exmdb_local_log_info(pcontext, address, 8,
			"rpc run-time error when delivering "
			"message into directory %s!", home_dir);
		return DELIVERY_OPERATION_FAILURE;
	case EXMDB_NO_SERVER:
		exmdb_local_log_info(pcontext, address, 8,
			"missing exmdb server connection when "
			"delivering message into directory %s!",
			home_dir);
		return DELIVERY_OPERATION_FAILURE;
	case EXMDB_RDWR_ERROR:
		exmdb_local_log_info(pcontext, address, 8,
			"read write error with exmdb server when"
			" delivering message into directory %s!",
			home_dir);
		return DELIVERY_OPERATION_FAILURE;
	case EXMDB_RESULT_ERROR:
		exmdb_local_log_info(pcontext, address, 8,
			"error result returned when delivering "
			"message into directory %s!", home_dir);
		return DELIVERY_OPERATION_FAILURE;
	case EXMDB_MAILBOX_FULL:
		exmdb_local_log_info(pcontext, address,
			8, "user's mailbox is full");
		return DELIVERY_MAILBOX_FULL;
	}
	return DELIVERY_OPERATION_FAILURE;
}

void exmdb_local_log_info(MESSAGE_CONTEXT *pcontext,
    const char *rcpt_to, int level, const char *format, ...)
{
	char log_buf[256];
	va_list ap;

	va_start(ap, format);
	vsnprintf(log_buf, sizeof(log_buf) - 1, format, ap);
	log_buf[sizeof(log_buf) - 1] = '\0';

	switch (pcontext->pcontrol->bound_type) {
	case BOUND_IN:
	case BOUND_OUT:
	case BOUND_RELAY:
		log_info(level, "SMTP message queue-ID: %d, FROM: %s, TO: %s  %s",
			pcontext->pcontrol->queue_ID, pcontext->pcontrol->from, rcpt_to,
			log_buf);
		break;
	default:
		log_info(level, "APP created message FROM: %s, TO: %s  %s",
			pcontext->pcontrol->from, rcpt_to, log_buf);
		break;
	}
}

void exmdb_local_console_talk(int argc,
	char **argv, char *result, int length)
{
	int tmp_port;
	int conn_num;
	int alive_num;
	CONFIG_FILE *pfile;
	char str_cache[64];
	char str_alarm[64];
	int response_interval;
	char str_interval[64];
	char str_response[16];
	char *ptr, tmp_ip[16];
	int scan_interval, retrying_times;
	int times, interval, alarm_interval;
	char help_string[] = "250 exmdb local help information:\r\n"
							 "\t%s status\r\n"
							 "\t    --print the running information\r\n"
							 "\t%s info\r\n"
							 "\t    --print the module information\r\n"
							 "\t%s bounce reload\r\n"
							 "\t    --reload the bounce resource list\r\n"
							 "\t%s set alarm-frequncy <times/interval>\r\n"
							 "\t    --set alarm frequency\r\n"
							 "\t%s set alarm-interval <interval>\r\n"
							 "\t    --set alarm interval\r\n"
							 "\t%s set cache-scan <interval>\r\n"
							 "\t    --set cache scanning interval\r\n"
							 "\t%s set retrying-times <times>\r\n"
							 "\t    --set the cache retrying times\r\n"
							 "\t%s set response-interval <interval>\r\n"
							 "\t    --set auto response interval\r\n"
							 "\t%s echo <prefix>\r\n"
							 "\t	--echo exmdb connection inforamtion";

	if (1 == argc) {
		strncpy(result, "550 too few arguments", length);
		return;
	}
	if (2 == argc && 0 == strcmp("--help", argv[1])) {
		snprintf(result, length, help_string, argv[0], argv[0], argv[0],
				argv[0], argv[0], argv[0], argv[0], argv[0], argv[0]);
		result[length - 1] = '\0';
		return;
	}
	if (2 == argc && 0 == strcmp("status", argv[1])) {
		snprintf(result, length,
					"250 mailbox local running information:\r\n"
					"\tOK                       %d\r\n"
					"\ttemporary fail           %d\r\n"
					"\tpermanent fail           %d\r\n"
					"\tno user                  %d",
					net_failure_get_param(NET_FAILURE_OK),
					net_failure_get_param(NET_FAILURE_TEMP),
					net_failure_get_param(NET_FAILURE_PERMANENT),
					net_failure_get_param(NET_FAILURE_NOUSER));
		return;
	}
	if (2 == argc && 0 == strcmp("info", argv[1])) {
		itvltoa(net_failure_get_param(NET_FAILURE_STATISTIC_INTERVAL),
			str_interval);
		itvltoa(net_failure_get_param(NET_FAILURE_ALARM_INTERVAL), str_alarm);
		itvltoa(cache_queue_get_param(CACHE_QUEUE_SCAN_INTERVAL), str_cache);
		itvltoa(bounce_audit_get_param(BOUNCE_AUDIT_INTERVAL), str_response);
		snprintf(result, length,
					"250 mailbox local module information:\r\n"
					"\tstatistic times          %d\r\n"
					"\tstatistic interval       %s\r\n"
					"\talarm interval           %s\r\n"
					"\tcache interval           %s\r\n"
					"\tretring times            %d\r\n"
					"\tresponse capacity        %d\r\n"
					"\tresponse interval        %s",
					net_failure_get_param(NET_FAILURE_STATISTIC_TIMES),
					str_interval,
					str_alarm,
					str_cache,
					cache_queue_get_param(CACHE_QUEUE_RETRYING_TIMES),
					bounce_audit_get_param(BOUNCE_AUDIT_CAPABILITY),
					str_response);
		return;
	}

	if (3 == argc && 0 == strcmp("bounce", argv[1]) &&
		0 == strcmp("reload", argv[2])) {
		if (TRUE == bounce_producer_refresh()) {
			strncpy(result, "250 bounce resource list reload OK", length);
		} else {
			strncpy(result, "550 bounce resource list reload error", length);
		}
		return;
	}
	
	if (4 == argc && 0 == strcmp("set", argv[1]) &&
		0 == strcmp("alarm-frequncy", argv[2])) {
		ptr = strchr(argv[3], '/');
		if (NULL == ptr) {
			snprintf(result, length, "550 invalid argument %s should be "
					"times/interval", argv[3]);
			return;
		}
		*ptr = '\0';
		times = atoi(argv[3]);
		interval = atoitvl(ptr + 1);
		if (times <=0 || interval <=0) {
			snprintf(result, length, "550 times and interval should larger "
				"than 0");
			return;
		}
		pfile = config_file_init2(NULL, g_config_path);
		if (NULL == pfile) {
			snprintf(result, length, "550 fail to open config file");
			return;
		}
		config_file_set_value(pfile, "FAILURE_TIMES_FOR_ALARM", argv[3]);
		config_file_set_value(pfile, "INTERVAL_FOR_FAILURE_STATISTIC", ptr + 1);
		if (FALSE == config_file_save(pfile)) {
			snprintf(result, length, "550 fail to save config file");
			config_file_free(pfile);
			return;
		}
		config_file_free(pfile);
		net_failure_set_param(NET_FAILURE_STATISTIC_TIMES, times);
		net_failure_set_param(NET_FAILURE_STATISTIC_INTERVAL, interval);
		snprintf(result, length, "250 frequency set OK");
		return;
	}
	if (4 == argc && 0 == strcmp("set", argv[1]) &&
		0 == strcmp("alarm-interval", argv[2])) {
		alarm_interval = atoitvl(argv[3]);
		if (alarm_interval <= 0) {
			snprintf(result, length, "550 invalid alram-interval %s", argv[3]);
			return;
		}
		pfile = config_file_init2(NULL, g_config_path);
		if (NULL == pfile) {
			snprintf(result, length, "550 fail to open config file");
			return;
		}
		config_file_set_value(pfile, "ALARM_INTERVAL", argv[3]);
		if (FALSE == config_file_save(pfile)) {
			snprintf(result, length, "550 fail to save config file");
			config_file_free(pfile);
			return;
		}
		config_file_free(pfile);
		net_failure_set_param(NET_FAILURE_ALARM_INTERVAL, alarm_interval);
		strncpy(result, "250 alarm-interval set OK", length);
		return;
	}
	if (4 == argc && 0 == strcmp("set", argv[1]) &&
		0 == strcmp("retrying-times", argv[2])) {
		retrying_times = atoi(argv[3]);
		if (retrying_times <= 0) {
			snprintf(result, length, "550 invalid retrying-times %s", argv[3]);
			return;
		}
		pfile = config_file_init2(NULL, g_config_path);
		if (NULL == pfile) {
			snprintf(result, length, "550 fail to open config file");
			return;
		}
		config_file_set_value(pfile, "RETRYING_TIMES", argv[3]);
		if (FALSE == config_file_save(pfile)) {
			snprintf(result, length, "550 fail to save config file");
			config_file_free(pfile);
			return;
		}
		config_file_free(pfile);
		cache_queue_set_param(CACHE_QUEUE_RETRYING_TIMES, retrying_times);
		strncpy(result, "250 retrying-times set OK", length);
		return;
	}
	if (4 == argc && 0 == strcmp("set", argv[1]) &&
		0 == strcmp("cache-scan", argv[2])) {
		scan_interval = atoitvl(argv[3]);
		if (scan_interval <=0 ) {
			snprintf(result, length, "550 invalid cache-scan %s", argv[3]);
			return;
		}
		pfile = config_file_init2(NULL, g_config_path);
		if (NULL == pfile) {
			snprintf(result, length, "550 fail to open config file");
			return;
		}
		config_file_set_value(pfile, "CACHE_SCAN_INTERVAL", argv[3]);
		if (FALSE == config_file_save(pfile)) {
			snprintf(result, length, "550 fail to save config file");
			config_file_free(pfile);
			return;
		}
		config_file_free(pfile);
		cache_queue_set_param(CACHE_QUEUE_SCAN_INTERVAL, scan_interval);
		strncpy(result, "250 cache-scan set OK", length);
		return;
	}
	if (4 == argc && 0 == strcmp("set", argv[1]) &&
		0 == strcmp("response-interval", argv[2])) {
		response_interval = atoitvl(argv[3]);
		if (response_interval <= 0) {
			snprintf(result, length, "550 invalid response-interval %s",
				argv[3]);
			return;
		}
		pfile = config_file_init2(NULL, g_config_path);
		if (NULL == pfile) {
			snprintf(result, length, "550 fail to open config file");
			return;
		}
		config_file_set_value(pfile, "RESPONSE_INTERVAL", argv[3]);
		if (FALSE == config_file_save(pfile)) {
			snprintf(result, length, "550 fail to save config file");
			config_file_free(pfile);
			return;
		}
		config_file_free(pfile);
		bounce_audit_set_param(BOUNCE_AUDIT_INTERVAL, response_interval);
		strncpy(result, "250 response-interval set OK", length);
		return;
	}
	if (3 == argc && 0 == strcmp("echo", argv[1])) {
		if (TRUE == exmdb_client_get_exmdb_information(argv[2],
			tmp_ip, &tmp_port, &conn_num, &alive_num)) {
			snprintf(result, length,
			"250 connection information of exmdb(dir:%s ip:%s port:%d):\r\n"
			"\ttotal connections       %d\r\n"
			"\tavailable connections   %d",
			argv[2], tmp_ip, tmp_port, conn_num, alive_num);
			result[length - 1] = '\0';
			return;
		}
		snprintf(result, length, "250 no inforamtion"
			" about exmdb(dir:%s)", argv[2]);
		return;
	}
	snprintf(result, length, "550 invalid argument %s", argv[1]);
	return;
}
