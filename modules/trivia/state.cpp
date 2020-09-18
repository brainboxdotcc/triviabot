/************************************************************************************
 * 
 * TriviaBot, The trivia bot for discord based on Fruitloopy Trivia for ChatSpike IRC
 *
 * Copyright 2004 Craig Edwards <support@brainbox.cc>
 *
 * Core based on Sporks, the Learning Discord Bot, Craig Edwards (c) 2019.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ************************************************************************************/

#include <sporks/modules.h>
#include <sporks/regex.h>
#include <string>
#include <cstdint>
#include <fstream>
#include <streambuf>
#include <sporks/stringops.h>
#include <sporks/statusfield.h>
#include <sporks/database.h>
#include "state.h"
#include "trivia.h"
#include "webrequest.h"

state_t::state_t(TriviaModule* _creator) : creator(_creator), terminating(false), channel_id(0), guild_id(0), numquestions(0), round(0), score(0), start_time(0), shuffle_list({}), gamestate(TRIV_ASK_QUESTION), curr_qid(0),
					recordtime(0), curr_question(""), curr_answer(""), curr_customhint1(""), curr_customhint2(""), curr_category(""), curr_lastasked(0), curr_recordtime(0), curr_lastcorrect(""),
					last_to_answer(0), streak(0), asktime(0), found(false), interval(20), insane_num(0), insane_left(0), curr_timesasked(0), next_quickfire(0), insane({}), timer(nullptr), shuffle1(""), shuffle2("")

{
}

state_t::~state_t()
{
	terminating = true;
	gamestate = TRIV_END;
	if (creator) {
		creator->GetBot()->core.log->debug("state_t::~state(): G:{} C:{}", guild_id, channel_id);
		creator->DisposeThread(timer);
	}
}

void state_t::tick()
{
	while (!terminating) {
		for (int j = 0; j < this->interval; j++) {
			sleep(1);
			if (terminating) {
				break;
			}
		}
		creator->Tick(this);
		int64_t game_length = time(NULL) - start_time;
		if (game_length >= GAME_REAP_SECS) {
			terminating = true;
			gamestate = TRIV_END;
			creator->GetBot()->core.log->debug("state_t::tick(): G:{} C:{} reaped game of length {} seconds", guild_id, channel_id, game_length);
		}
	}
}


