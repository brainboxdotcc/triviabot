#!/bin/sh
#
# TriviaBot, the Discord Quiz Bot with over 80,000 questions!
#
# Copyright 2004 Craig Edwards <support@sporks.gg>
#
# Core based on Sporks, the Learning Discord Bot, Craig Edwards (c) 2019.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
cd "$2"
# run gdb in batch mode to generate mail the stack trace of the latest crash to the error_recipeint from config.json
/usr/bin/gdb -batch -ex "set pagination off" -ex "set height 0" -ex "set width 0" -ex "bt full" ./bot $(ls -Art ./*core* | tail -n 1) | grep -v ^"No stack."$ | mutt -s "TriviaBot cluster $1 rebooted, stack trace generated" -- $(/usr/bin/jq -r '.error_recipient' ../config.json)
