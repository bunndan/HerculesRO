// Copyright (c) Hercules Dev Team, licensed under GNU GPL.
// See the LICENSE file
// Portions Copyright (c) Athena Dev Teams

#define HERCULES_CORE

#include "ipban.h"

#include <stdlib.h>
#include <string.h>

#include "login.h"
#include "loginlog.h"
#include "../common/cbasetypes.h"
#include "../common/db.h"
#include "../common/malloc.h"
#include "../common/sql.h"
#include "../common/socket.h"
#include "../common/strlib.h"
#include "../common/timer.h"
#include "../common/showmsg.h"
#include "../common/conf.h"

// Sql settings
static char   ipban_db_hostname[32] = "127.0.0.1";
static uint16 ipban_db_port = 3306;
static char   ipban_db_username[32] = "ragnarok";
static char   ipban_db_password[32] = "ragnarok";
static char   ipban_db_database[32] = "ragnarok";
static char   ipban_codepage[32] = "";
static char   ipban_table[32] = "ipbanlist";

// globals
static Sql* sql_handle = NULL;
static int cleanup_timer_id = INVALID_TIMER;
static bool ipban_inited = false;

int ipban_cleanup(int tid, int64 tick, int id, intptr_t data);


// initialize
void ipban_init(void)
{
	const char* username;
	const char* password;
	const char* hostname;
	uint16      port;
	const char* database;
	const char* codepage;

	ipban_inited = true;

	if( !login_config.ipban )
		return;// ipban disabled

	username = ipban_db_username;
	password = ipban_db_password;
	hostname = ipban_db_hostname;
	port     = ipban_db_port;
	database = ipban_db_database;
	codepage = ipban_codepage;

	// establish connections
	sql_handle = SQL->Malloc();
	if( SQL_ERROR == SQL->Connect(sql_handle, username, password, hostname, port, database) )
	{
		Sql_ShowDebug(sql_handle);
		SQL->Free(sql_handle);
		exit(EXIT_FAILURE);
	}
	if( codepage[0] != '\0' && SQL_ERROR == SQL->SetEncoding(sql_handle, codepage) )
		Sql_ShowDebug(sql_handle);

	if( login_config.ipban_cleanup_interval > 0 )
	{ // set up periodic cleanup of connection history and active bans
		timer->add_func_list(ipban_cleanup, "ipban_cleanup");
		cleanup_timer_id = timer->add_interval(timer->gettick()+10, ipban_cleanup, 0, 0, login_config.ipban_cleanup_interval*1000);
	} else // make sure it gets cleaned up on login-server start regardless of interval-based cleanups
		ipban_cleanup(0,0,0,0);
}

// finalize
void ipban_final(void)
{
	if( !login_config.ipban )
		return;// ipban disabled

	if( login_config.ipban_cleanup_interval > 0 )
		// release data
		timer->delete(cleanup_timer_id, ipban_cleanup);
	
	ipban_cleanup(0,0,0,0); // always clean up on login-server stop

	// close connections
	SQL->Free(sql_handle);
	sql_handle = NULL;
}

/**
 * Reads login_configuration.account.ipban.dynamic_pass_failure and loads configuration options
 * @param cfgName path to configuration file
 * @retval false in case of failure
 **/
static bool ipban_config_read_dynamic( const char* cfgName, config_t *config ) {
	config_setting_t *setting;

	if( !(setting = libconfig->lookup(config, "login_configuration.account.ipban.dynamic_pass_failure")) ) {
		ShowError("account_db_sql_set_property: login_configuration.account.ipban.dynamic_pass_failure"
					"was not found in %s!\n",
					cfgName);
		return false;
	}

	libconfig->setting_lookup_bool_real(setting, "enabled", &login_config.dynamic_pass_failure_ban);
	libconfig->setting_lookup_uint32(setting, "ban_interval", &login_config.dynamic_pass_failure_ban_interval);
	libconfig->setting_lookup_uint32(setting, "ban_limit", &login_config.dynamic_pass_failure_ban_limit);
	libconfig->setting_lookup_uint32(setting, "ban_duration", &login_config.dynamic_pass_failure_ban_duration);

	return true;
}

/**
 * Reads login_configuration.account.ipban.sql_connection and loads configuration options
 * @param cfgName path to configuration file
 * @retval false in case of failure
 **/
static bool ipban_config_read_connection( const char* cfgName, config_t *config ) {
	config_setting_t *setting;

	if( !(setting = libconfig->lookup(config, "login_configuration.account.ipban.sql_connection")) ) {
		ShowError("account_db_sql_set_property: login_configuration.account.ipban.sql_connection was not found in %s!\n",
			cfgName);
		return false;
	}

	libconfig->setting_lookup_mutable_string(setting, "db_hostname", ipban_db_hostname, sizeof(ipban_db_hostname));
	libconfig->setting_lookup_mutable_string(setting, "db_database", ipban_db_database, sizeof(ipban_db_database));

	libconfig->setting_lookup_mutable_string(setting, "db_username", ipban_db_username, sizeof(ipban_db_username));
	libconfig->setting_lookup_mutable_string(setting, "db_password", ipban_db_password, sizeof(ipban_db_password));
	libconfig->setting_lookup_mutable_string(setting, "codepage", ipban_codepage, sizeof(ipban_codepage));
	libconfig->setting_lookup_uint16(setting, "db_port", &ipban_db_port);

	return true;
}

/**
 * Reads 'inter_configuration' and initializes required variables
 * Sets global configuration
 * @param cfgName path to configuration file
 * @retval false in case of failure
 **/
static bool ipban_config_read_inter( const char* cfgName ) {
	config_t config;
	config_setting_t *setting;

	if( libconfig->read_file(&config, cfgName) )
		return false; // Error message is already shown by libconfig->read_file

	if( !(setting = libconfig->lookup(&config, "inter_configuration.database_names")) ) {
		ShowError("ipban_config_read: inter_configuration.database_names was not found!\n");
		return false;
	}
	libconfig->setting_lookup_mutable_string(setting, "ipban_table", ipban_table, sizeof(ipban_table));

	return true;
}

/**
 * Reads login_configuration.account.ipban and loads configuration options
 * @param cfgName path to configuration file
 * @retval false in case of failure
 **/
bool ipban_config_read( const char* cfgName, config_t *config ) {
	config_setting_t *setting;

	if( ipban_inited )
		return false; // settings can only be changed before init

	if( !(setting = libconfig->lookup(config, "login_configuration.account.ipban")) ) {
		ShowError("login_config_read: login_configuration.log was not found in %s!\n", cfgName);
		return false;
	}

	libconfig->setting_lookup_bool_real(setting, "enabled", &login_config.ipban);
	libconfig->setting_lookup_uint32(setting, "cleanup_interval", &login_config.ipban_cleanup_interval);

	ipban_config_read_inter("conf/inter-server.conf");
	ipban_config_read_connection(cfgName, config);
	ipban_config_read_dynamic(cfgName, config);

	return true;
}

// check ip against active bans list
bool ipban_check(uint32 ip)
{
	uint8* p = (uint8*)&ip;
	char* data = NULL;
	int matches;

	if( !login_config.ipban )
		return false;// ipban disabled

	if( SQL_ERROR == SQL->Query(sql_handle, "SELECT count(*) FROM `%s` WHERE `rtime` > NOW() AND (`list` = '%u.*.*.*' OR `list` = '%u.%u.*.*' OR `list` = '%u.%u.%u.*' OR `list` = '%u.%u.%u.%u')",
		ipban_table, p[3], p[3], p[2], p[3], p[2], p[1], p[3], p[2], p[1], p[0]) )
	{
		Sql_ShowDebug(sql_handle);
		// close connection because we can't verify their connectivity.
		return true;
	}

	if( SQL_ERROR == SQL->NextRow(sql_handle) )
		return true;// Shouldn't happen, but just in case...

	SQL->GetData(sql_handle, 0, &data, NULL);
	matches = atoi(data);
	SQL->FreeResult(sql_handle);

	return( matches > 0 );
}

// log failed attempt
void ipban_log(uint32 ip)
{
	unsigned long failures;

	if( !login_config.ipban )
		return;// ipban disabled

	failures = loginlog_failedattempts(ip, login_config.dynamic_pass_failure_ban_interval);// how many times failed account? in one ip.

	// if over the limit, add a temporary ban entry
	if( failures >= login_config.dynamic_pass_failure_ban_limit )
	{
		uint8* p = (uint8*)&ip;
		if( SQL_ERROR == SQL->Query(sql_handle, "INSERT INTO `%s`(`list`,`btime`,`rtime`,`reason`) VALUES ('%u.%u.%u.*', NOW() , NOW() +  INTERVAL %d MINUTE ,'Password error ban')",
			ipban_table, p[3], p[2], p[1], login_config.dynamic_pass_failure_ban_duration) )
			Sql_ShowDebug(sql_handle);
	}
}

// remove expired bans
int ipban_cleanup(int tid, int64 tick, int id, intptr_t data) {
	if( !login_config.ipban )
		return 0;// ipban disabled

	if( SQL_ERROR == SQL->Query(sql_handle, "DELETE FROM `%s` WHERE `rtime` <= NOW()", ipban_table) )
		Sql_ShowDebug(sql_handle);

	return 0;
}
