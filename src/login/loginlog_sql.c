// Copyright (c) Hercules Dev Team, licensed under GNU GPL.
// See the LICENSE file
// Portions Copyright (c) Athena Dev Teams

#define HERCULES_CORE

#include "loginlog.h"

#include <string.h>
#include <stdlib.h> // exit

#include "../common/cbasetypes.h"
#include "../common/mmo.h"
#include "../common/socket.h"
#include "../common/sql.h"
#include "../common/strlib.h"
#include "../common/showmsg.h"
#include "../common/conf.h"

// global sql settings (in ipban_sql.c)
static char   global_db_hostname[32] = "127.0.0.1";
static uint16 global_db_port = 3306;
static char   global_db_username[32] = "ragnarok";
static char   global_db_password[32] = "ragnarok";
static char   global_db_database[32] = "ragnarok";
static char   global_codepage[32] = "";
// local sql settings
static char   log_db_hostname[32] = "";
static uint16 log_db_port = 0;
static char   log_db_username[32] = "";
static char   log_db_password[32] = "";
static char   log_db_database[32] = "";
static char   log_codepage[32] = "";
static char   log_login_db[256] = "loginlog";

static Sql* sql_handle = NULL;
static bool enabled = false;


// Returns the number of failed login attemps by the ip in the last minutes.
unsigned long loginlog_failedattempts(uint32 ip, unsigned int minutes)
{
	unsigned long failures = 0;

	if( !enabled )
		return 0;

	if( SQL_ERROR == SQL->Query(sql_handle, "SELECT count(*) FROM `%s` WHERE `ip` = '%s' AND `rcode` = '1' AND `time` > NOW() - INTERVAL %d MINUTE",
		log_login_db, ip2str(ip,NULL), minutes) )// how many times failed account? in one ip.
		Sql_ShowDebug(sql_handle);

	if( SQL_SUCCESS == SQL->NextRow(sql_handle) )
	{
		char* data;
		SQL->GetData(sql_handle, 0, &data, NULL);
		failures = strtoul(data, NULL, 10);
		SQL->FreeResult(sql_handle);
	}
	return failures;
}


/*=============================================
 * Records an event in the login log
 *---------------------------------------------*/
void login_log(uint32 ip, const char* username, int rcode, const char* message)
{
	char esc_username[NAME_LENGTH*2+1];
	char esc_message[255*2+1];
	int retcode;

	if( !enabled )
		return;

	SQL->EscapeStringLen(sql_handle, esc_username, username, strnlen(username, NAME_LENGTH));
	SQL->EscapeStringLen(sql_handle, esc_message, message, strnlen(message, 255));

	retcode = SQL->Query(sql_handle,
		"INSERT INTO `%s`(`time`,`ip`,`user`,`rcode`,`log`) VALUES (NOW(), '%s', '%s', '%d', '%s')",
		log_login_db, ip2str(ip,NULL), esc_username, rcode, esc_message);

	if( retcode != SQL_SUCCESS )
		Sql_ShowDebug(sql_handle);
}

bool loginlog_init(void)
{
	const char* username;
	const char* password;
	const char* hostname;
	uint16      port;
	const char* database;
	const char* codepage;

	if( log_db_hostname[0] != '\0' )
	{// local settings
		username = log_db_username;
		password = log_db_password;
		hostname = log_db_hostname;
		port     = log_db_port;
		database = log_db_database;
		codepage = log_codepage;
	}
	else
	{// global settings
		username = global_db_username;
		password = global_db_password;
		hostname = global_db_hostname;
		port     = global_db_port;
		database = global_db_database;
		codepage = global_codepage;
	}

	sql_handle = SQL->Malloc();

	if( SQL_ERROR == SQL->Connect(sql_handle, username, password, hostname, port, database) )
	{
		Sql_ShowDebug(sql_handle);
		SQL->Free(sql_handle);
		exit(EXIT_FAILURE);
	}

	if( codepage[0] != '\0' && SQL_ERROR == SQL->SetEncoding(sql_handle, codepage) )
		Sql_ShowDebug(sql_handle);

	enabled = true;

	return true;
}

bool loginlog_final(void)
{
	SQL->Free(sql_handle);
	sql_handle = NULL;
	return true;
}

/**
 * Reads 'inter_configuration.sql_connection.log_database' and initializes required variables
 * Sets global configuration
 * @param cfgName path to configuration file
 * @retval false in case of failure
 **/
bool loginlog_config_read_inter_log( const char* cfgName ) {
	config_t config;
	config_setting_t *setting;

	if( libconfig->read_file(&config, cfgName) )
		return false; // Error message is already shown by libconfig->read_file

	if( !(setting = libconfig->lookup(&config, "inter_configuration.sql_connection.log_database")) ) {
		ShowError("account_db_sql_set_property: inter_configuration.sql_connection.log_database was not found!\n");
		return false;
	}

	libconfig->setting_lookup_string_char(setting, "db_ip", log_db_hostname, sizeof(log_db_hostname));
	libconfig->setting_lookup_string_char(setting, "db_db", global_db_database, sizeof(global_db_database));
	libconfig->setting_lookup_string_char(setting, "db_id", log_db_username, sizeof(log_db_username));
	libconfig->setting_lookup_string_char(setting, "db_db", log_db_password, sizeof(log_db_password));

	libconfig->setting_lookup_uint16(setting, "db_port", &log_db_port);
	libconfig->setting_lookup_string_char(setting, "codepage", log_codepage, sizeof(log_codepage));
	libconfig->setting_lookup_string_char(setting, "login_db", log_login_db, sizeof(log_login_db));

	return true;
}

/**
 * Reads 'inter_configuration' and initializes required variables
 * Sets global configuration
 * @param cfgName path to configuration file
 * @retval false in case of failure
 **/
bool loginlog_config_read_inter( const char* cfgName ) {
	config_t config;
	config_setting_t *setting;
	int error =0;

	if( libconfig->read_file(&config, cfgName) )
		return false; // Error message is already shown by libconfig->read_file

	if( !(setting = libconfig->lookup(&config, "inter_configuration.sql_connection")) ) {
		ShowError("account_db_sql_set_property: inter_configuration.sql_connection was not found!\n");
		return false;
	}

	libconfig->setting_lookup_string_char(setting, "db_hostname", global_db_hostname, sizeof(global_db_hostname));
	libconfig->setting_lookup_string_char(setting, "db_database", global_db_database, sizeof(global_db_database));
	libconfig->setting_lookup_string_char(setting, "db_username", global_db_username, sizeof(global_db_username));
	libconfig->setting_lookup_string_char(setting, "db_password", global_db_password, sizeof(global_db_password));

	libconfig->setting_lookup_uint16(setting, "db_port", &global_db_port);
	libconfig->setting_lookup_string_char(setting, "codepage", global_codepage, sizeof(global_codepage));

	return true;
}

bool loginlog_config_read( void )
{
	loginlog_config_read_inter("conf/inter-server.conf");
	loginlog_config_read_inter_log("conf/inter-server.conf");

	return true;
}
