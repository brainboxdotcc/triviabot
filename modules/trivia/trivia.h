#pragma once

#include <string>
#include <map>
#include <vector>

typedef std::map<int64_t, int64_t> teamlist_t;
typedef std::map<int64_t, std::string> numstrs_t;

enum trivia_state_t
{
	TRIV_ASK_QUESTION = 1,
	TRIV_FIRST_HINT = 2,
	TRIV_SECOND_HINT = 3,
	TRIV_TIME_UP = 4,
	TRIV_ANSWER_CORRECT = 5,
	TRIV_END = 6
};

#define TRIV_INTERVAL 20

class state_t
{
 public:
	uint32_t numquestions;
	uint32_t round;
	std::vector<int64_t> shuffle_list;
	trivia_state_t gamestate;
	int64_t curr_qid;
	std::string curr_question;
	std::string curr_answer;
	std::string curr_customhint1;
	std::string curr_customhint2;
	std::string curr_category;
	time_t curr_lastasked;
	time_t curr_recordtime;
	std::string curr_lastcorrect;
	int64_t last_to_answer;
	uint32_t streak;
	time_t asktime;
	bool found;
	time_t interval;
	uint32_t insane_num;
	uint32_t insane_left;
	time_t next_quickfire;
	std::map<std::string, bool> insane;
};
