<?php

/**********************************************************************************************************
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
 ***********************************************************************************************************/

$settings = json_decode(file_get_contents("config.json"));
$conn = mysqli_connect($settings->dbhost, $settings->dbuser, $settings->dbpass);

use Carbon\Carbon;
require_once getenv("HOME") . '/www/functions.php';

$config["live_bot"] = $settings->livetoken;


if (!$conn) {
	die("Can't connect to database, check config.json\n");
}

mysqli_select_db($conn, $settings->dbname);

$lastId = 0;
do {
    $subscriptions = botApiRequest(sprintf("v10/applications/%s/entitlements?exclude_ended=false&limit=100&after=$lastId", $settings->application_id));

    $now = new Carbon();
    foreach ($subscriptions as $subscription) {
        if (in_array($subscription->type, [6, 8])) {
           $start = new Carbon($subscription->starts_at ?? null);
	    $end = new Carbon($subscription->ends_at ?? null);
	    echo "SUBSCRIPTION: $subscription->subscription_id USER: $subscription->user_id GUILD: $subscription->guild_id ACTIVE: " . ($now->between($start, $end) ? 1 : 0) . " ";
	    echo "START: " .(is_object($start) ? $start->format('Y-m-d H:i:s') : 'N/A') . " END: " . (is_object($end) ? $end->format('Y-m-d H:i:s') : 'N/A') . " TYPE: " . $subscription->type . "\n\n";
	    if (!empty($subscription->subscription_id)) {
                mysqli_query($conn, "INSERT INTO premium_credits (user_id, subscription_id, guild_id, active, since, plan_id, payment_failed) VALUES($subscription->user_id, $subscription->subscription_id, $subscription->guild_id, 1, now(), 'triviabot-premium-monthly', 0) ON DUPLICATE KEY UPDATE active = " . ($now->between($start, $end) ? 1 : 0));
            }
            $lastId = $subscription->id;
        }
    }
} while (count($subscriptions) === 100);
