# Sporks, the backchatting, learning, scriptable discord bot
This github project contains the source code for Sporks, the learning, scriptable Discord chat bot, written in C++ using the aegis.cpp library.
Remember you can still find my original perl/botnix version of Sporks on IRC at irc.chatspike.net!

## Project and System status

![Discord](https://img.shields.io/discord/537746810471448576?label=discord) ![CircleCI](https://img.shields.io/circleci/build/github/brainboxdotcc/sporks/master) ![Dashboard](https://img.shields.io/website?down_color=red&label=dashboard&url=https%3A%2F%2Fsporks.gg)

[Service Status](https://status.sporks.gg)

## Listing Badges

[![Discord Bots](https://top.gg/api/widget/630730262765895680.svg)](https://top.gg/bot/630730262765895680)
![Discord Boats](https://discord.boats/api/widget/630730262765895680) 
![DiscordBotList](https://discordbotlist.com/bots/630730262765895680/widget) [![Bots for Discord](https://botsfordiscord.com/api/bot/630730262765895680/widget)](https://botsfordiscord.com/bots/630730262765895680)

## Supported Platforms

Currently only Linux is supported, but other UNIX-style platforms should build and run the bot fine. I build the bot under Debian Linux.

## Dependencies

* [cmake](https://cmake.org/) (version 3.13+)
* [g++](https://gcc.gnu.org) (version 8+)
* [aegis.cpp](https://github.com/zeroxs/aegis.cpp) (development branch)
* [asio](https://think-async.com/Asio/) (included with aegis.cpp)
* [websocketpp](https://github.com/zaphoyd/websocketpp) (included with aegis.cpp)
* [nlohmann::json](https://github.com/nlohmann/json) (included with aegis.cpp)
* [duktape](https://github.com/svaarala/duktape) (master branch)
* [PCRE](https://www.pcre.org/) (whichever -dev package comes with your OS)
* [MySQL Client Libraries](https://dev.mysql.com/downloads/c-api/) (whichever -dev package comes with your OS)
* [ZLib](https://www.zlib.net/) (whichever -dev package comes with your OS)
 
## Building

    mkdir build
    cmake ..
    make -j8
    
Replace the number after -j with a number suitable for your setup, usually the same as the number of cores on your machine.

## Database

You should have a database configured with the mysql schemas from the mysql-schemas directory. use mysqlimport to import this.

## Configuration

Edit the config-example.json file and save it as config.json. The configuration variables in the file should be self explainatory.

## Running

    cd my-bot-dir
    ./run.sh

run.sh will restart the bot executable continually if it dies. 

## Command line parameters

    ./bot [--dev|--test] [--members]

| Argument        | Meaning                                                |
| --------------- |------------------------------------------------------- |
| --dev           | Run using the development token in the config file. Mutually exclusive with ``--test``     |
| --test          | Run using the live token in the config file, but squelch all outbound messages unless they originate from the test server (also defined in the config file)  |
| --members       | Send a GUILD_MEMBERS intent when identifying to the discord gateway |
