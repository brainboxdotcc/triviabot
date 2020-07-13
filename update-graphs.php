<?php

/**********************************************************************************************************
 *
 * TriviaBot, the Discord Quiz Bot with over 80,000 questions!
 *
 * Copyright 2004 Craig Edwards <support@sporks.gg>
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
 ***********************************************************************************************************/

$settings = json_decode(file_get_contents("config.json"));
$conn = mysqli_connect($settings->dbhost, $settings->dbuser, $settings->dbpass);

if (!$conn) {
        die("Can't connect to database, check config.json\n");
}

mysqli_select_db($conn, $settings->dbname);

mysqli_query($conn, "DELETE FROM infobot_cpu_graph WHERE logdate < now() - INTERVAL 1 DAY");
$cpu_percent =  trim(`ps aux | grep "./bot " | grep -v grep | awk -F ' ' '{ print $3 }'`);
$current = mysqli_fetch_object(mysqli_query($conn, "SELECT * FROM infobot_discord_counts WHERE dev = 0"));
mysqli_query($conn, "INSERT INTO trivia_graphs (entry_date, cpu, user_count, server_count, channel_count, memory_usage, games) VALUES(now(), $cpu_percent, $current->user_count, $current->server_count, $current->channel_count, $current->memory_usage, $current->games)");
