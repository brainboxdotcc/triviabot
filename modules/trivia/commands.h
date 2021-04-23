#pragma once

#include <string>
#include "settings.h"
#include <dpp/dpp.h>

#define DECLARE_COMMAND_CLASS(__command_name__) class __command_name__ : public command_t { public: __command_name__(class TriviaModule* _creator, const std::string &_base_command); virtual void call(const in_cmd &cmd, std::stringstream &tokens, guild_settings_t &settings, const std::string &username, bool is_moderator, dpp::channel* c, dpp::user* user); };

class in_cmd
{
 public:
	std::string msg;
	std::string username;
	int64_t author_id;
	int64_t channel_id;
	int64_t guild_id;
	bool mentions_bot;
	bool from_dashboard;
	in_cmd(const std::string &m, int64_t author, int64_t channel, int64_t guild, bool mention, const std::string &username, bool dashboard);
};

class command_t 
{
 protected:
 	 class TriviaModule* creator;
	std::string base_command;
	std::string _(const std::string &str, const guild_settings_t &settings);
 public:
	command_t(class TriviaModule* _creator, const std::string &_base_command);
	virtual void call(const in_cmd &cmd, std::stringstream &tokens, guild_settings_t &settings, const std::string &username, bool is_moderator, dpp::channel* c, dpp::user* user) = 0;
	virtual ~command_t();
};

DECLARE_COMMAND_CLASS(command_start_t);
DECLARE_COMMAND_CLASS(command_stop_t);
DECLARE_COMMAND_CLASS(command_vote_t);
DECLARE_COMMAND_CLASS(command_votehint_t);
DECLARE_COMMAND_CLASS(command_stats_t);
DECLARE_COMMAND_CLASS(command_info_t);
DECLARE_COMMAND_CLASS(command_join_t);
DECLARE_COMMAND_CLASS(command_create_t);
DECLARE_COMMAND_CLASS(command_leave_t);
DECLARE_COMMAND_CLASS(command_help_t);

typedef std::map<std::string, command_t*> command_list_t;
