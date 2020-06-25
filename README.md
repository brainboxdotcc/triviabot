# TriviaBot, the discord bot with 88,000 questions!
This project contains the source code for the Brainbox.cc TriviaBot. Written in C++17 using Aegis.cpp and ASIO.

It was originally FruitLoopy Trivia on irc.chatspike.net and i've been running this bot in some form since 2004.


This source code repository is for reference and learning only as it **wont work without a TriviaBot API key**, which i'm not prepared to give out to people (no, this isn't "*open trivia database*"). Without this key, the bot will start, but will be unable to start any games, or retrieve any statistics or update the cache.

## Project and System status

![Discord](https://img.shields.io/discord/537746810471448576?label=discord) ![Dashboard](https://img.shields.io/website?down_color=red&label=dashboard&url=https%3A%2F%2Ftriviabot.co.uk)

[Service Status](https://status.triviabot.co.uk)

## Listing Badges

[![Discord Bots](https://top.gg/api/widget/715906723982082139.svg)](https://top.gg/bot/715906723982082139)
![Discord Boats](https://discord.boats/api/widget/715906723982082139) 
![DiscordBotList](https://discordbotlist.com/bots/715906723982082139/widget) [![Bots for Discord](https://botsfordiscord.com/api/bot/715906723982082139/widget)](https://botsfordiscord.com/bots/715906723982082139)

## Supported Platforms

Currently only Linux is supported, but other UNIX-style platforms should build and run the bot fine. I build the bot under Debian Linux.

## Dependencies

* [cmake](https://cmake.org/) (version 3.13+)
* [g++](https://gcc.gnu.org) (version 8+)
* gdb for debugging on development servers (any recent version)
* [aegis.cpp](https://github.com/zeroxs/aegis.cpp) (development branch)
* [asio](https://think-async.com/Asio/) (included with aegis.cpp)
* [websocketpp](https://github.com/zaphoyd/websocketpp) (included with aegis.cpp)
* [nlohmann::json](https://github.com/nlohmann/json) (included with aegis.cpp)
* [PCRE](https://www.pcre.org/) (whichever -dev package comes with your OS)
* [MySQL Client Libraries](https://dev.mysql.com/downloads/c-api/) (whichever -dev package comes with your OS)
* [ZLib](https://www.zlib.net/) (whichever -dev package comes with your OS)

# Setup

## 1. Build Source Code

    mkdir build
    cmake ..
    make -j8
    
Replace the number after -j with a number suitable for your setup, usually the same as the number of cores on your machine.

## 2. Setup Database

You should have a database configured with the mysql schemas from the mysql-schemas directory. use mysqlimport to import this. Note that the database schema included only has the bare minimum tables to boot the client bot. There is no question database structure, or API schema included in this dump.

## 3. Edit Configuration File

Edit the config-example.json file and save it as config.json. The configuration variables in the file should be self explainatory.

## 4. Start Bot

Before the initial startup, you should first make a log directory inside your build directory:

    mkdir my-bot-dir/build/log

Once this is done, you will be able to start the bot so long as you've set up the database and configuration file, above.

    cd my-bot-dir
    ./run.sh

run.sh will restart the bot executable continually if it dies. If the bot quits, ``run.sh`` will trigger ``mail-core-fire.sh`` which will email a coredump to the admin user, if GDB is installed.

### Command line parameters

    ./bot [--dev|--test] [--members]

| Argument        | Meaning                                                |
| --------------- |------------------------------------------------------- |
| --dev           | Run using the development token in the config file. Mutually exclusive with ``--test``     |
| --test          | Run using the live token in the config file, but squelch all outbound messages unless they originate from the test server (also defined in the config file)  |
| --members       | Send a GUILD_MEMBERS intent when identifying to the discord gateway. Note that you'll need this after October 2020 for bots in over 100 servers. |

# Version History

| Version/Date    | Protocol              | Platform/Libraries                                                                 | Language            | Status       |
| ----------------|-----------------------|------------------------------------------------------------------------------------|---------------------|--------------|
| 1.0 / 2004      | IRC                   | [WinBot](https://www.winbot.co.uk) 2.3                                             | WinBotScript (WBS)  | Discontinued |
| 2.0 / 2005      | IRC                   | [WinBot](https://www.winbot.co.uk) 2.3                                             | PlugPerl (Perl 5.6) | Discontinued |
| 3.0 / 2005      | IRC                   | [Botnix](https://www.botnix.org) 1.0 Beta 1                                        | Perl 5.6            | Discontinued |
| 4.0 / 2020      | IRC                   | [Discord](https://discord.com), [aegis.cpp](https://github.com/zeroxs/aegis.cpp)   | C++17               | Active       |
