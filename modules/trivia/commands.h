#pragma once

#include <string>
#include <map>
#include "settings.h"
#include <dpp/dpp.h>

#define DECLARE_COMMAND_CLASS(__command_name__, __ancestor__) class __command_name__ : public __ancestor__ { public: __command_name__(class TriviaModule* _creator, const std::string &_base_command, bool adm, const std::string& descr, std::vector<dpp::command_option> options); virtual void call(const in_cmd &cmd, std::stringstream &tokens, guild_settings_t &settings, const std::string &username, bool is_moderator, dpp::channel* c, dpp::user* user); virtual ~__command_name__() = default; };
#define DECLARE_COMMAND_CLASS_SELECT(__command_name__, __ancestor__) class __command_name__ : public __ancestor__ { public: __command_name__(class TriviaModule* _creator, const std::string &_base_command, bool adm, const std::string& descr, std::vector<dpp::command_option> options); virtual void call(const in_cmd &cmd, std::stringstream &tokens, guild_settings_t &settings, const std::string &username, bool is_moderator, dpp::channel* c, dpp::user* user); virtual ~__command_name__() = default; virtual void select_click(const dpp::select_click_t & event, const in_cmd &cmd, guild_settings_t &settings); virtual void button_click(const dpp::button_click_t & event, const in_cmd &cmd, guild_settings_t &settings);  };

#define BLANK_EMOJI "<:blank:667278047006949386>"

class in_cmd
{
 public:
	std::string msg;
	std::string username;
	dpp::user user;
	dpp::guild_member member;
	uint64_t author_id;
	uint64_t channel_id;
	uint64_t guild_id;
	bool mentions_bot;
	bool from_dashboard;
	std::string interaction_token;
	dpp::snowflake command_id;
	in_cmd(const std::string &m, uint64_t author, uint64_t channel, uint64_t guild, bool mention, const std::string &username, bool dashboard, dpp::user u, dpp::guild_member gm);
};

class command_t 
{
 protected:
 	 class TriviaModule* creator;
	std::string base_command;
	std::string _(const std::string &str, const guild_settings_t &settings);
 public:
	bool admin;
	bool ephemeral;
	dpp::slashcommand_contextmenu_type type;
 	std::string description;
	std::vector<dpp::command_option> opts;
	command_t(class TriviaModule* _creator, const std::string &_base_command, bool adm, const std::string& descr, std::vector<dpp::command_option> options, bool is_ephemeral = false, dpp::slashcommand_contextmenu_type command_type = dpp::ctxm_chat_input);
	virtual void call(const in_cmd &cmd, std::stringstream &tokens, guild_settings_t &settings, const std::string &username, bool is_moderator, dpp::channel* c, dpp::user* user) = 0;
	virtual void select_click(const dpp::select_click_t & event, const in_cmd &cmd, guild_settings_t &settings);
	virtual void button_click(const dpp::button_click_t & event, const in_cmd &cmd, guild_settings_t &settings);
	virtual ~command_t();
};

DECLARE_COMMAND_CLASS(command_start_t, command_t);
DECLARE_COMMAND_CLASS(command_stop_t, command_t);
DECLARE_COMMAND_CLASS(command_vote_t, command_t);
DECLARE_COMMAND_CLASS(command_votehint_t, command_t);
DECLARE_COMMAND_CLASS(command_stats_t, command_t);
DECLARE_COMMAND_CLASS(command_info_t, command_t);
DECLARE_COMMAND_CLASS(command_join_t, command_t);
DECLARE_COMMAND_CLASS(command_create_t, command_t);
DECLARE_COMMAND_CLASS(command_leave_t, command_t);
DECLARE_COMMAND_CLASS(command_help_t, command_t);
DECLARE_COMMAND_CLASS(command_dashboard_t, command_t);
DECLARE_COMMAND_CLASS(command_global_t, command_t);
DECLARE_COMMAND_CLASS(command_enable_t, command_t);
DECLARE_COMMAND_CLASS(command_disable_t, command_t);
DECLARE_COMMAND_CLASS(command_privacy_t, command_t);
DECLARE_COMMAND_CLASS(command_invite_t, command_t);
DECLARE_COMMAND_CLASS(command_coins_t, command_t);
DECLARE_COMMAND_CLASS(command_language_t, command_t);
DECLARE_COMMAND_CLASS(command_prefix_t, command_t);
DECLARE_COMMAND_CLASS(command_forceleave_t, command_t);
DECLARE_COMMAND_CLASS(command_topteams_t, command_t);
DECLARE_COMMAND_CLASS(command_nitro_t, command_t);
DECLARE_COMMAND_CLASS(command_resetprefix_t, command_t);
DECLARE_COMMAND_CLASS(command_shard_t, command_t);
DECLARE_COMMAND_CLASS(command_give_t, command_t);
DECLARE_COMMAND_CLASS(command_queue_t, command_t);
DECLARE_COMMAND_CLASS(command_categories_t, command_t);
DECLARE_COMMAND_CLASS(command_team_t, command_t);
DECLARE_COMMAND_CLASS(command_ping_t, command_t);
DECLARE_COMMAND_CLASS(command_servertime_t, command_t);
DECLARE_COMMAND_CLASS(command_achievements_t, command_t);
DECLARE_COMMAND_CLASS(command_profile_t, command_t);
DECLARE_COMMAND_CLASS_SELECT(command_context_user_t, command_t);
DECLARE_COMMAND_CLASS(command_context_message_t, command_context_user_t);

typedef std::multimap<std::string, command_t*> command_list_t;
