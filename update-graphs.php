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

$total_shards = 4;

$settings = json_decode(file_get_contents("config.json"));
$conn = mysqli_connect($settings->dbhost, $settings->dbuser, $settings->dbpass);

if (!$conn) {
	die("Can't connect to database, check config.json\n");
}

mysqli_select_db($conn, $settings->dbname);

require_once("../www/conf.php");
require_once("../www/functions.php");

$s = hrtime(true);
$r = botApiRequest("channels/537746810471448580", 'GET');
$discord_api_ping = (hrtime(true) - $s) / 1000000;
$s = hrtime(true);
file_get_contents("http://triviabot.co.uk/api/");
$tb_api_ping = (hrtime(true) - $s) / 1000000;
$s = hrtime(true);
mysqli_query($link, "SHOW TABLES");
$db_ping = (hrtime(true) - $s) / 1000000;

$fifteen_mins_ago = time() - (60*15);
$kicks = 0;
$cmds = 0;

$fh = fopen("/home/trivia/triviabot/build/log/aegis.log", "r");
while (!feof($fh)) {
	$l = trim(fgets($fh));
	$m = [];
	if (preg_match('/(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\./', $l, $m)) {
		$d = strtotime($m[1]);
		if ($d > $fifteen_mins_ago) {
			if (preg_match('/Guild \d+ deleted \(bot kicked\?\), removing active game states/i', $l)) {
				$kicks++;
			} else if (preg_match('/CMD \(USER=\d+, GUILD=\d+\): /', $l)) {
				$cmds++;
			}
		}
	}

}

$last = mysqli_fetch_object(mysqli_query($conn, "SELECT * FROM trivia_graphs ORDER BY id DESC LIMIT 1"));
$check = mysqli_fetch_object(mysqli_query($conn, "SELECT COUNT(online) AS online, COUNT(connected) AS connected FROM infobot_shard_status"));

mysqli_query($conn, "DELETE FROM infobot_cpu_graph WHERE logdate < now() - INTERVAL 1 DAY");
$cpu_percent =  trim(`ps aux | grep "./bot " | grep -v grep | awk -F ' ' '{ print $3 }'`);

if ($check->online < $total_shards || $check->connected < $total_shards) {
	mysqli_query($conn, "INSERT INTO trivia_graphs (entry_date, cpu, user_count, server_count, channel_count, memory_usage, games, discord_ping, trivia_ping, db_ping, kicks, commands) VALUES(now(), $cpu_percent, $last->user_count, $last->server_count, $last->channel_count, $last->memory_usage, $last->games, $discord_api_ping, $tb_api_ping, $db_ping, $kicks, $cmds)");
} else {
	$current = mysqli_fetch_object(mysqli_query($conn, "SELECT * FROM infobot_discord_counts WHERE dev = 0"));
	mysqli_query($conn, "INSERT INTO trivia_graphs (entry_date, cpu, user_count, server_count, channel_count, memory_usage, games, discord_ping, trivia_ping, db_ping, kicks, commands) VALUES(now(), $cpu_percent, $current->user_count, $current->server_count, $current->channel_count, $current->memory_usage, $current->games, $discord_api_ping, $tb_api_ping, $db_ping, $kicks, $cmds)");
}

