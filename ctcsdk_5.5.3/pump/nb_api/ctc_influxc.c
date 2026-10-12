#if (SDK_WORK_PLATFORM == 1)
#include <curl/curl.h>
#else
#include "../lib/curl.h"
#endif
#include "ctc_influxc.h"

#define INFLUXDB_URL_MAX_SIZE 1000
#define INFLUXDB_BUF_SIZE 512

typedef struct ctc_influxc_string_s
{
	char* ptr;
	uint32 len;
}ctc_influxc_str_t;

char*
influxdb_strdup(const char* str)
{
    char* dst;
    uint32 len = 0;
    if (!str)
    {
        return NULL;
    }
    len = sal_strlen(str);
    if (0 == len)
    {
        return NULL;
    }
    dst = sal_malloc(sizeof(char) * (len + 1));
    if (!dst)
    {
        return NULL;
    }
    sal_strcpy(dst, str);
    return dst;
}

void
init_string(ctc_influxc_str_t *s)
{
    if (NULL == s)
    {
        return;
    }
	s->len = 0;
	s->ptr = (char*)sal_malloc(s->len+1);
	if (s->ptr == NULL)
	{
		return;
	}
	s->ptr[0] = '\0';
}

uint32
append_string(ctc_influxc_str_t* s, char* append)
{
	uint32 new_len = 0;

    if (!s || !append)
    {
        return 0;
    }
    new_len = s->len;

	if(append)
	{
		new_len = s->len + sal_strlen(append);
		s->ptr = (char*)sal_realloc(s->ptr,new_len+1);
		if(s->ptr == NULL)
		{
			new_len = 0;
		}
		else
		{
			sal_memcpy(s->ptr + s->len, append, sal_strlen(append));
			s->ptr[new_len] = '\0';
			s->len = new_len;
		}
	}
	return new_len;
}


int
_ctc_influxc_client_get_url(
	ctc_influxc_client_t* client,
	char (*buffer)[],
	uint32 size,
	char* path)
{
    (*buffer)[0] = '\0';
    sal_strncat(*buffer, client->schema, size);
    sal_strncat(*buffer, "://", size);
    sal_strncat(*buffer, client->host, size);
    sal_strncat(*buffer, path, size);
    return sal_strlen(*buffer);
}

uint32
_ctc_influxc_client_write_data(
	char* buf,
	uint32 size,
	uint32 nmemb,
	void* userdata)
{
    uint32 realsize = size * nmemb;
    if (userdata != NULL)
    {
    /*
         char** buffer = userdata;
        *buffer = sal_realloc(*buffer, sal_strlen(*buffer) + realsize + 1);
        if (*buffer)
        {
            sal_strncat(*buffer, buf, realsize);
        }
        */
    }
    return realsize;
}

int
_ctc_influxc_client_curl(char* url, char* reqtype,char* body, char* usrpwd, char** response)
{
    CURLcode c;
    CURL* handle = curl_easy_init();

    if (!handle)
    {
        return -1;
    }

    if (reqtype)
    {
        curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, reqtype);
    }
    if (url)
    {
        curl_easy_setopt(handle, CURLOPT_URL, url);
    }
    if (usrpwd)
    {
        curl_easy_setopt(handle, CURLOPT_USERPWD, usrpwd);
    }
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, _ctc_influxc_client_write_data);
    if (response)
    {
        curl_easy_setopt(handle, CURLOPT_WRITEDATA, response);
    }

    curl_easy_setopt(handle, CURLOPT_TIMEOUT, 30);

    if (body)
    {
        curl_easy_setopt(handle, CURLOPT_POSTFIELDS, body);
    }
    c = curl_easy_perform(handle);

    if (c == CURLE_OK)
    {
        long status_code = 0;
        if (curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status_code) == CURLE_OK)
            c = status_code;
    }
    curl_easy_cleanup(handle);
    return c;
}

uint32
_ctc_influxc_writefunc(void* ptr,uint32 size, uint32 nmemb, ctc_influxc_str_t* s)
{
	uint32 new_len = 0;
    if (!s)
    {
        return 0;
    }
    new_len = s->len + size*nmemb;
	s->ptr = (char*)sal_realloc(s->ptr, new_len+1);
	if (!s->ptr)
	{
		return 0;
	}
	sal_memcpy(s->ptr+s->len, ptr, size*nmemb);
	s->ptr[new_len] = '\0';
	s->len = new_len;
	return size*nmemb;
}

int
ctc_influxc_get_response_body(char* url, char* usrpwd, ctc_influxc_str_t* outstr)
{
	CURL* curl;

	curl = curl_easy_init();
	if(curl)
	{
		init_string(outstr);
        if (url)
        {
    		curl_easy_setopt(curl, CURLOPT_URL,url);
        }
        if (usrpwd)
        {
            curl_easy_setopt(curl, CURLOPT_USERPWD, usrpwd);
        }
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _ctc_influxc_writefunc);
        if (outstr)
        {
    		curl_easy_setopt(curl, CURLOPT_WRITEDATA,outstr);
        }
		curl_easy_perform(curl);
		/* always cleanup */
		curl_easy_cleanup(curl);
	}
	return 0;
}

int
_ctc_influxc_get_userpwd(ctc_influxc_client_t* client, uint32 len, char* usrpwd)
{
    if (!client || !len || !usrpwd)
    {
        return -1;
    }
    if (client->uname && client->password && sal_strlen(client->uname) && sal_strlen(client->password))
    {
        sal_snprintf(usrpwd,len, "%s:%s", client->uname, client->password);
    }
    return 0;
}

int
ctc_influxc_client_get(ctc_influxc_client_t* client, char* path, char** res)
{
    int status;
    char url[INFLUXDB_URL_MAX_SIZE];
    char usrpwd[INFLUXDB_BUF_SIZE] = {0};
    char* buffer = (char*)sal_malloc(sizeof(char));

    if (!buffer)
    {
        return -1;
    }
    if (!client)
    {
        sal_free(buffer);
        return -1;
    }
    sal_memset(buffer, 0, sizeof(char));
    _ctc_influxc_get_userpwd(client, INFLUXDB_BUF_SIZE, usrpwd);

    _ctc_influxc_client_get_url(client, &url, INFLUXDB_URL_MAX_SIZE, path);

    status = _ctc_influxc_client_curl(url, NULL, NULL, usrpwd, &buffer);

    if (status >= 200 && status < 300 && res != NULL)
        *res = buffer;

    return status;
}

int
ctc_influxc_client_post(ctc_influxc_client_t* client,char* path,char* body)
{
    char url[INFLUXDB_URL_MAX_SIZE];
    char usrpwd[INFLUXDB_BUF_SIZE] = {0};

    if (!client)
    {
        return -1;
    }
    _ctc_influxc_get_userpwd(client, INFLUXDB_BUF_SIZE, usrpwd);
    _ctc_influxc_client_get_url(client, &url, INFLUXDB_URL_MAX_SIZE, path);
    return  _ctc_influxc_client_curl(url, NULL, body, usrpwd, NULL);
}

int
_ctc_influxc_op_database(ctc_influxc_client_t *client, char* op)
{
	char buf[INFLUXDB_BUF_SIZE]={0};
    if (!client || !op)
    {
        return -1;
    }

	sal_snprintf(buf, INFLUXDB_BUF_SIZE, "q=%s DATABASE %s",op, client->dbname);
    return ctc_influxc_client_post(client, "/query",buf);
}

void
ctc_influxc_client_free(ctc_influxc_client_t* client)
{
    if (!client)
    {
        return;
    }

    if (client->uname)
        {sal_free(client->uname);}
    if (client->password)
        {sal_free(client->password);}
    if (client->dbname)
        {sal_free(client->dbname);}
    if (client->host)
        {sal_free(client->host);}

    sal_free(client);
}

ctc_influxc_client_t*
ctc_influxc_client_new(char* host,char* uname,char* password,char* dbname,char ssl)
{
    ctc_influxc_client_t *client = sal_malloc(sizeof(ctc_influxc_client_t));
    if (!client)
    {
        return NULL;
    }

	if (!uname) uname = "";
	if (!password) password = "";
	if (!dbname)
	{
		sal_free(client);
		client = NULL;
	}
	else
	{
    	client->schema = ssl ? "https" : "http";
    	client->host = influxdb_strdup(host);
    	client->uname = influxdb_strdup(uname);
    	client->password = influxdb_strdup(password);
    	client->dbname = influxdb_strdup(dbname);
    	client->ssl = ssl;
	}
    return client;
}

int
ctc_influxc_create_user(ctc_influxc_client_t *client)
{
	char buf[INFLUXDB_BUF_SIZE]={0};
    if (!client)
    {
        return -1;
    }

	sal_snprintf(buf, INFLUXDB_BUF_SIZE, "q=CREATE USER %s WITH PASSWORD '%s' WITH ALL PRIVILEGES", client->uname, client->password);
    return ctc_influxc_client_post(client, "/query",buf);
}

int
ctc_influxc_delete_user(ctc_influxc_client_t *client)
{
	char buf[INFLUXDB_BUF_SIZE]={0};
    if (!client)
    {
        return -1;
    }
	sal_snprintf(buf, INFLUXDB_BUF_SIZE, "q=DROP USER %s", client->uname);
    return ctc_influxc_client_post(client, "/query",buf);
}

int
ctc_influxc_create_database(ctc_influxc_client_t* client)
{
    return _ctc_influxc_op_database(client, "CREATE");
}

int
ctc_influxc_delete_database(ctc_influxc_client_t *client)
{
    return _ctc_influxc_op_database(client, "DROP");
}

int
ctc_influxc_insert_measurement(ctc_influxc_client_t* client, char* measurement)
{
	char path[INFLUXDB_URL_MAX_SIZE] = {0};
    if (!client || !measurement)
    {
        return -1;
    }

	sal_snprintf(path, INFLUXDB_URL_MAX_SIZE, "/write?db=%s", client->dbname);
	return ctc_influxc_client_post(client, path, measurement);
}

int
ctc_influxc_delete_measurement(ctc_influxc_client_t* client, char* m_name)
{
	char path[INFLUXDB_URL_MAX_SIZE] = {0};
    char body[INFLUXDB_URL_MAX_SIZE] = {0};

    if (!client || !m_name)
    {
        return -1;
    }
	sal_snprintf(path, INFLUXDB_URL_MAX_SIZE, "/query?db=%s",client->dbname);
	sal_snprintf(body, INFLUXDB_URL_MAX_SIZE,"q=DROP MEASUREMENT %s",m_name);
	return ctc_influxc_client_post(client,path,body);
}

int
ctc_influxc_query_measurement(ctc_influxc_client_t* client, char* str, ctc_influxc_str_t* outstr)
{
    char url[INFLUXDB_URL_MAX_SIZE];
	CURL *curl = curl_easy_init();
	char path[INFLUXDB_URL_MAX_SIZE] = {0};
    char usrpwd[INFLUXDB_BUF_SIZE] = {0};
	sal_snprintf(path, INFLUXDB_URL_MAX_SIZE, "/query?db=%s&q=",client->dbname);
	if (curl)
	{
		char* output = curl_easy_escape(curl, str, sal_strlen(str));
		if (output)
		{
			sal_strncat(path,output,INFLUXDB_URL_MAX_SIZE);
			curl_free(output);
		}
		curl_easy_cleanup(curl);
	}
    _ctc_influxc_get_userpwd(client, INFLUXDB_BUF_SIZE, usrpwd);
    _ctc_influxc_client_get_url(client, &url, INFLUXDB_URL_MAX_SIZE, path);
    return ctc_influxc_get_response_body(url, usrpwd, outstr);
}

int
ctc_influxc_create_policy(ctc_influxc_client_t* client,
                                char* policy_name, char* time, char is_default, char is_update)
{
	char buf[INFLUXDB_BUF_SIZE] = {0};
    char* op = is_update ? "ALTER" : "CREATE";

    if (!client || !policy_name || !time)
    {
        return -1;
    }

	sal_snprintf(buf, INFLUXDB_BUF_SIZE, "q=%s RETENTION POLICY %s ON %s DURATION %s SHARD DURATION %s REPLICATION 1", op, policy_name, client->dbname, time, time);
    if (is_default)
    {
        sal_strcat(buf, " DEFAULT");
    }
    return ctc_influxc_client_post(client, "/query",buf);
}

int
ctc_influxc_delete_policy(ctc_influxc_client_t* client, char* policy_name)
{
	char buf[INFLUXDB_BUF_SIZE] = {0};

    if (!client || !policy_name)
    {
        return -1;
    }

	sal_snprintf(buf, INFLUXDB_BUF_SIZE, "q=DROP RETENTION POLICY %s ON %s",policy_name, client->dbname);
    return ctc_influxc_client_post(client, "/query",buf);
}


