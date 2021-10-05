<?php

$against = $argv[1];

$file_list = explode("\n", `/usr/bin/git diff --diff-filter=ACMR --name-only $against`);

foreach ($file_list as $file) {
	$file = trim($file);
	if (!empty($file) && preg_match('/^modules\/.+?.cpp$/', $file)) {
		$source = file_get_contents($file);
		$version = 1;
		$found = false;
		$source = preg_replace_callback('/\$ModVer (\d+)\$/', function(array $matches) {
			$current_version = intval($matches[1]) + 1;
			$version = $current_version;
			$found = true;
			return "\$ModVer $current_version$";
		}, $source);
		if ($found) {
			$source = preg_replace('/\$ModVer\$/m', '$ModVer 1$', $source);
			print "$file => version $version\n";
			file_put_contents($file, $source);
			system("/usr/bin/git add $file");
		}
	}
}
