<?php

$settings = json_decode(file_get_contents("config.json"));
$conn = mysqli_connect($settings->dbhost, $settings->dbuser, $settings->dbpass);

if (!$conn) {
        die("Can't connect to database, check config.json\n");
}

mysqli_select_db($conn, $settings->dbname);

mysqli_query($conn, "DELETE FROM infobot_cpu_graph WHERE logdate < now() - INTERVAL 1 DAY");
$cpu_percent =  trim(`ps aux | grep "./bot " | grep -v grep | awk -F ' ' '{ print $3 }'`);
$current = mysqli_fetch_object(mysqli_query($conn, "SELECT * FROM infobot_discord_counts WHERE dev = 0"));
mysqli_query($conn, "INSERT INTO trivia_graphs (entry_date, cpu, user_count, server_count, channel_count, memory_usage) VALUES(now(), $cpu_percent, $current->user_count, $current->server_count, $current->channel_count, $current->memory_usage)");
