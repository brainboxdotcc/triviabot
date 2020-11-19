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
cd build
# enable core dumps for debugging
ulimit -c unlimited
# run repeatedly until ctrl+c
while true;
do
	./bot -members -clusterid 0 -maxclusters 8
	../mail-core-file.sh 0 $(pwd)
done

