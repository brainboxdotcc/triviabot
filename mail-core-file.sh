#!/bin/sh
#
# Sporks, the learning, scriptable Discord bot!
#
# Copyright 2019 Craig Edwards <support@sporks.gg>
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
cd $1
# run gdb in batch mode to generate mail the stack trace of the latest crash to the error_recipeint from config.json
/usr/bin/gdb -batch -ex "set pagination off" -ex "set height 0" -ex "set width 0" -ex "bt full" ./bot `ls -Art *core* | tail -n 1` | grep -v ^"No stack."$ | mutt -s "Sporks Bot rebooted, stack trace and log attached" -a log/aegis.log -- `/usr/bin/jq -r '.error_recipient' ../config.json`
