<?php

/***********************************************************************************************************
 * 
 * Sporks, the learning, scriptable Discord bot!
 *
 * Copyright 2019 Craig Edwards <support@sporks.gg>
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
 *
 * Sporks UptimeRobot Heartbeat Ping
 *
 * This script should be run every minute under crontab, and uses UptimeRobot's API to retrieve the
 * heartbeat url for each monitor associated with a discord shard. It will find these by requesting all
 * monitors which contain the string "Discord Bot Shard " in their name, and are of type 5 (heartbeat).
 * Once they are retrieved it will iterate them and match them to a regular expression by friendly name,
 * extracting the shard ID from the name. It then does two things:
 *
 * 1) Update the database to store the last uptimerobot heartbeat url to be pinged under infobot_shard_status
 *
 * 2) If the shard is both online and connected, ping the endpoint with a simple GET request to let
 *    uptimerobot know this shard is alive
 *
 * If this script does not ping one of the uptimerobot heartbeat endpoints for 4 minutes (two heartbeats)
 * then uptimerobot will POST to a discord webhook which causes an embed message in #status on the official
 * discord server.
 *
 ***********************************************************************************************************/

$settings = json_decode(file_get_contents("config.json"));
$conn = mysqli_connect($settings->dbhost, $settings->dbuser, $settings->dbpass);

if (!$conn) {
	die("Can't connect to database, check config.json\n");
}

mysqli_select_db($conn, $settings->dbname);

$payload = [
	'api_key' => $settings->utr_readonly_key,
	'format' => 'json',
	'logs' => '0',
	'types' => '5',
	'search' => 'TriviaBot Shard ',
];
$context = [
	'http' => [
			'method' => 'POST',
			'ignore_errors' => true,
			'header'  => "Content-Type: application/x-www-form-urlencoded\r\nCache-Control: no-cache",
			'content' => http_build_query($payload),
		]
	];
$response = @file_get_contents("https://api.uptimerobot.com/v2/getMonitors", false, stream_context_create($context));
$monitors = json_decode($response);
foreach($monitors->monitors as $monitor) {
	if (preg_match("/^TriviaBot Shard (\d+)$/", $monitor->friendly_name, $m)) {
		$shard_id = $m[1];
		$url = $monitor->url;
		$shardinfo = mysqli_fetch_object(mysqli_query($conn, "SELECT * FROM infobot_shard_status WHERE id = " . mysqli_real_escape_string($conn, $m[1])));
		if ($shardinfo) {
			mysqli_query($conn, "UPDATE infobot_shard_status SET uptimerobot_heartbeat = '" . mysqli_real_escape_string($conn, $monitor->url) . "' WHERE id = " . mysqli_real_escape_string($conn, $m[1]));
			if ($shardinfo->connected && $shardinfo->online && time() - strtotime($shardinfo->updated) < 90) {
				$utr_response = file_get_contents($monitor->url);
				mysqli_query($conn, "UPDATE infobot_shard_status SET uptimerobot_response = '" . mysqli_real_escape_string($conn, $utr_response) . "' WHERE id = " . mysqli_real_escape_string($conn, $m[1]));
			}
		}
	}
}
