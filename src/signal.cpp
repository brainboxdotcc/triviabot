/************************************************************************************
 * 
 * TriviaBot, the Discord Quiz Bot with over 80,000 questions!
 *
 * Copyright 2004,2005,2020,2021,2024 Craig Edwards <support@brainbox.cc>
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

#include <csignal>
#include <sporks/bot.h>

bool reload = false;

/**
 * Set up signals to ignore some common non-fatals.
 * We'll use SIGHUP for commandline rehash.
 */
void Bot::SetSignals()
{
	signal(SIGALRM, SIG_IGN);
	signal(SIGHUP, &Bot::SetSignal);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGCHLD, SIG_IGN);
	signal(SIGXFSZ, SIG_IGN);
}

/**
 * It's not recommended to do a lot inside a signal handler,
 * so set a flag and look out for this to do a reload.
 * Reloading via SIGHUP isn't actually implemented yet,
 * so this flag is ignored by the rest of the program.
 */
void Bot::SetSignal(int signal)
{
	switch (signal) {
		case SIGHUP:
			reload = true;
		break;
		default:
		break;
	}
}

