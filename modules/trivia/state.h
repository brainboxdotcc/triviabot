#pragma once
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <deque>

enum trivia_state_t
{
	TRIV_ASK_QUESTION = 1,
	TRIV_FIRST_HINT = 2,
	TRIV_SECOND_HINT = 3,
	TRIV_TIME_UP = 4,
	TRIV_ANSWER_CORRECT = 5,
	TRIV_END = 6
};

class in_msg
{
 public:
	std::string msg;
	std::string username;
	int64_t author_id;
	bool mentions_bot;
	in_msg(const std::string &m, int64_t author, bool mention, const std::string &username);
};

struct question_t
{
	int64_t id;
	std::string question;
	std::string answer;
	std::string customhint1;
	std::string customhint2;
	std::string catname;
	time_t lastasked;
	int32_t timesasked;
	std::string lastcorrect;
	double recordtime;
	std::string shuffle1;
	std::string shuffle2;
	std::string question_image;
	std::string answer_image;

	question_t();
	question_t(int64_t _id, const std::string &_question, const std::string &_answer, const std::string &_hint1, const std::string &_hint2, const std::string &_catname, time_t _lastasked, int32_t _timesasked,
		const std::string &_lastcorrect, double _record_time, const std::string &_shuffle1, const std::string &_shuffle2, const std::string &_question_image, const std::string &_answer_image);

	static question_t fetch(int64_t id, int64_t guild_id, const class guild_settings_t &settings);
};

class state_t
{
	class TriviaModule* creator;
	std::string _(const std::string &k, const class guild_settings_t& settings);
	uint32_t get_activity();
	void record_activity(uint64_t user_id);
	bool should_drop_coin();
	void do_insane_board();

 public:
	time_t next_tick;
	bool terminating;
	uint64_t channel_id;
	uint64_t guild_id;
	uint32_t numquestions;
	uint32_t round;
	uint32_t score;
	time_t start_time;
	std::vector<std::string> shuffle_list;
	trivia_state_t gamestate;
	question_t question;
	std::string original_answer;
	uint64_t last_to_answer;
	uint32_t streak;
	time_t asktime;
	bool found;
	time_t interval;
	uint32_t insane_num;
	uint32_t insane_left;
	time_t next_quickfire;
	bool hintless;
	std::map<std::string, bool> insane;
	std::map<uint64_t, time_t> activity;

	state_t(const state_t &) = default;
	state_t();
	state_t(class TriviaModule* _creator, uint32_t questions, uint32_t currstreak, int64_t lastanswered, uint32_t question_index, uint32_t _interval, int64_t channel_id, bool hintless, const std::vector<std::string> &shuffle_list, trivia_state_t startstate,  int64_t guild_id);
	~state_t();
	void tick();
	void queue_message(const std::string &message, int64_t author_id, const std::string &username, bool mentions_bot = false);
	void handle_message(const in_msg& m);
	bool is_valid();
	void do_insane_round(bool silent);
	void do_normal_round(bool silent);
	void do_first_hint();
	void do_second_hint();
	void do_time_up();
	void do_answer_correct();
	void do_end_game();
	void StopGame(const guild_settings_t &settings);
	bool is_insane_round();
};

