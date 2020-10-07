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
 *
 * Bot list updater for Botnix
 *
 * There are a ton of discord bot listing sites out there, they all seem to have similar-ish APIs which let
 * you update your guild count and user count via a http POST with an Authorisation header. To allow this to
 * be done on a more or less generic basis, this script accepts details of these sites from a mysql table
 * (infobot_discord_list sites) and submits totals to them from all shards. Each shard has individual
 * responsibility for updating its row in the infobot_discord_counts table into the mysql cluster.
 *
 * Don't crontab this script more often than every few minutes, as you don't weant to annoy these sites by
 * spamming their API endpoints with updates!
 *
 ***********************************************************************************************************/

$settings = json_decode(file_get_contents("config.json"));
$conn = mysqli_connect($settings->dbhost, $settings->dbuser, $settings->dbpass);

if (!$conn) {
	die("Can't connect to database, check config.json\n");
}

mysqli_select_db($conn, $settings->dbname);
/* Fetch statistics from all shards for non-development instances */
$totals = mysqli_fetch_object(mysqli_query($conn, "SELECT SUM(user_count) AS users, SUM(server_count) AS servers, MAX(shard_count) AS shards, SUM(sent_messages) AS sent_messages, SUM(received_messages) AS received_messages, MAX(memory_usage) AS memory_usage, SUM(channel_count) AS channels FROM infobot_discord_counts WHERE dev = 0"));

/* Get a list of all bot listing sites */
$q = mysqli_query($conn, "SELECT * FROM infobot_discord_list_sites");
while ($site = mysqli_fetch_object($q)) {
	$payload = new stdClass;
	$payload->{$site->server_count_field} = $totals->servers + 0;
	if (!empty($site->user_count_field)) {
		$payload->{$site->user_count_field} = $totals->users + 0;
	}
	if (!empty($site->shard_count_field)) {
		$payload->{$site->shard_count_field} = $totals->shards + 0;
	}
        if (!empty($site->sent_message_count_field)) {
                $payload->{$site->sent_message_count_field} = $totals->sent_messages + 0;
        }
        if (!empty($site->received_message_count_field)) {
                $payload->{$site->received_message_count_field} = $totals->received_messages + 0;
        }
        if (!empty($site->ram_used_field)) {
                $payload->{$site->ram_used_field} = $totals->memory_usage *1024;
	}
	if (!empty($site->channels_field)) {
		$payload->{$site->channels_field} = $totals->channels + 0;
	}
	if (!empty($site->auth_field)) {
		$payload->{$site->auth_field} = $site->authorization;
	}
	$json_payload = json_encode($payload);
	$ct = ($site->post_type == 'json' ? 'application/json' : 'application/x-www-form-urlencoded');
	/* Note there's a special case here for wonderbotlist, a french bot list site that just has to be different in a traditionally french way! ;) */
	$context = [
		'http' => [
				'method' => 'POST',
				'ignore_errors' => true,
				'header'  => "Content-Type: $ct\r\nAuthorization: " . $site->authorization.  "\r\nX-TOKEN-DBLFR: " . $site->authorization,
				'content' => ($site->post_type == 'json' ? $json_payload : http_build_query($payload)), 
			]
		];
	$response = @file_get_contents($site->url, false, stream_context_create($context));
	echo $site->url . " => " . $response . " (" . $http_response_header[0] . ")\n";
}

