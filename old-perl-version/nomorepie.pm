package modules::nomorepie;

use LWP::UserAgent;
use Time::HiRes qw(time);
use POSIX;

my $numquestions = 1;
my @shuffle_list = ();
my %teams = ();
my $state = 0;
my $curr_qid = 0;
my $curr_question = "";
my $curr_answer = "";
my $curr_customhint1 = "";
my $curr_customhint2 = "";
my $curr_category = "";
my $curr_lastasked = 0;
my $curr_timesasked = 0;
my $curr_lastcorrect = "";
my $curr_recordtime = 0;
my $last_to_answer = "";
my $streak = 1;
my $asktime = time();
my %insane = ();
my @insane = ();
my $found = 0;
my $interval = 20;
my $real_total = 100;
my $insane_num = 0;
my $left = 0;
my $next_quickfire = time() - 1;
my @numberstrs = ();
my $notvowel = qr/[^aeiou_]/i; # _ is because of \w


my $url = "http://trivia.chatspike.net";

sub init {
	my ($self) = @_;
	main::create_timer("trivia_timer",$self,"timer_function",$interval);
	open(FH,"/home/trivia/numstrs.txt");
	@numberstrs = ();
	while (chomp($line = <FH>)) {
		push @numberstrs, $line;
	}
	t();
}

sub t {
	print "\n\nMakeFirstHint(12345): " . MakeFirstHint(12345) . "\n";
	print "MakeFirstHint(0): " . MakeFirstHint(0) . "\n";
	print "dec_to_roman(15): " . dec_to_roman(15) . "\n";
	print "conv_num('two thousand one hundred and fifty four'): " . conv_num("two thousand one hundred and fifty four") . "\n";
	print "conv_num('five'): " . conv_num("five") . "\n";
	print "conf_num('ten pin bowling'): " . conv_num("ten pin bowling") . "\n";
	print "conv_num('zero'): " . conv_num("zero") . "\n";
	print "scramble('abcdef'): " .scramble("abcdef") ."\n";
	print "scramble('A'): " .scramble("A") ."\n";
	print "piglatin('easy with the pig latin my friend'): " . piglatin("easy with the pig latin my friend") . "\n";
	print "conv_num('one million dollars'): " . conv_num("one million dollars") . "\n";
	print "tidy_num('\$1,000,000'): " . tidy_num('$1,000,000') . "\n";
	print "tidy_num('1,000'): " . tidy_num('1,000') . "\n";
	print "tidy_num('1000'): " . tidy_num('1000') . "\n";
	print "tidy_num('asdfghjk'): " .tidy_num('asdfghjk') . "\n";
	print "tidy_num('abc def ghi'): " . tidy_num('abc def ghi') . "\n";
	print "tidy_num('1000 dollars') " . tidy_num('1000 dollars') . "\n";
	print "tidy_num('1,000 dollars') " . tidy_num('1,000 dollars') . "\n";
	print "tidy_num('1,000 armadillos') " . tidy_num('1,000 armadillos') . "\n";
	print "tidy_num('27 feet') " . tidy_num('27 feet') . "\n";
	print "tidy_num('twenty seven feet') " . tidy_num('twenty seven feet') . "\n";
	print "letterlong('a herd of gnus') " .letterlong('a herd of gnus') . "\n";
	print "vowelcount('a herd of gnus') " .vowelcount('a herd of gnus') . "\n";
	print "levenshtein('a herd of cows','a herd of wocs') " . levenshtein('a herd of cows','a herd of wocs') . "\n";
	print "levenshtein('Cows','coWz')  " . levenshtein('Cows','coWz') . "\n";
	exit(0);
}

sub implements {
	t();
        my @functions = ("on_privmsg","on_join");
        return @functions;
}

sub shutdown {
	my ($self,$nid,$server,$nick,$ident,$host,$channel)= @_;
	main::delete_timer("trivia_timer");
}

sub pm {
	main::send_privmsg('chatspike','#trivia',"\0038,2\002[\002 $_[0] \002]\002\003");
}

sub debug {
	main::send_privmsg('chatspike','#NoMorePie',"$_[0]");
}

sub dec_to_roman {
	my @numbers = (1, 4, 5, 9, 10, 40, 50, 90, 100, 400, 500, 900, 1000);
	my @romans = ('I', 'IV', 'V', 'IX', 'X', 'XL', 'L', 'XC', 'C', 'CD', 'D', 'CM', 'M');
	my $result = "";
	my $decimal = shift || 0;
	for ($x = 12; $x >= 0; $x--) {
		while ($decimal >= $numbers[$x]) {
			$decimal -= $numbers[$x];
			$result .= $romans[$x];
		}
	}
	return "Roman Numerals: $result";
}

sub tidy_num {
	my $num = $_[0];
	if ($num =~ /^([\d\,]+)\s+dollars$/i) {
		$num = "\$$1";
	}
	if ($num =~ /^([\d\,]+)\s+(.+?)$/i) {
		my $n1 = $1;
		my $n2 = $2;
		$n1 =~ s/,//g;
		$num = "$n1 $n2";
	}
	if (($num =~ /^\$[\d,]+$/) or ($num =~ /^[\d\,]+$/) or ($num =~ /^-[\d\,]+$/)) {
		$num =~ s/,//g;
	}
	return $num;
}

sub conv_num {
	my %nn = 	(
				"one" => 1,
				"two" => 2,
				"three" => 3,
				"four" => 4,
				"five" => 5,
				"six" => 6,
				"seven" => 7,
				"eight" => 8,
				"nine" => 9,
				"ten" => 10,
				"eleven" => 11,
				"twelve" => 12,
				"thirteen" => 13,
				"fourteen" => 14,
				"forteen" => 14,
				"fifteen" => 15,
				"sixteen" => 16,
				"seventeen" => 17,
				"eighteen" => 18,
				"nineteen" => 19,
				"twenty" => 20,
				"thirty" => 30,
				"fourty" => 40,
				"forty" => 40,
				"fifty" => 50,
				"sixty" => 60,
				"seventy" => 70,
				"eighty" => 80,
				"ninety" => 90,
			);
			
	my $datain = shift || "zero";
	$datain =~ s/\s\s/ /g;
	$datain =~ tr/\-/ /;
	$datain =~ s/\s+and\s+/ /gi;
	my @nums = split(' ',$datain);
	my $last = 0;
	my $initial = 0;
	my $currency = "";
	foreach my $check (@nums) {
		if ((!defined ($nn{lc($check)})) && ($check !~ /million/i) && ($check !~ /thousand/i) && ($check !~ /hundred/i) && ($check !~ /dollars/i)) {
			return 0;
		}
	}
	for (0 .. scalar(@nums)) {
		$nextnum = lc($nums[$_]);
		my $lookahead = lc($nums[$_+1]);
		if (defined ($nn{$nextnum})) {
			$last = $nn{$nextnum};
		}
		if ($nextnum =~ /dollars/i) {
			$currency = '$';
			$last = 0;
		}
		if ($lookahead !~ /(hundred|thousand|million)/i) {
			$initial += $last;
			$last = 0;
		} else {
			if ($lookahead =~ /hundred/i) {
				$initial += $last*100;
				$last = 0;
			} elsif ($lookahead =~ /thousand/i) {
				$initial += $last*1000;
				$last = 0;
			} elsif ($lookahead =~ /million/i) {
				$initial += $last*1000000;
				$last = 0;
			}
		}
	}
	return "$currency$initial";
}

sub scramble {
	my $return = $_[0];
	my $x = 0;
	while ((lc($return) eq lc($_[0])) && ($x < 50)) {
		$return = "";
		foreach my $letter (split('',$_[0])) {
			$return = (rand(10) > 4 ? $return . $letter : $letter . $return);
		}
		$x++;
	}
	return "Scrambled answer: " . lc($return);
}


sub piglatin {
	local $_ = shift;

	# Match the word
	s[(\w+)]
	{
		local $_ = $1;
		# Now re-arrange the leading consonants
		#  or if none, append "yay"
		s/^($notvowel+)(.*)/$2$1ay/
		or
		s/$/yay/;
		$_; # Return the result
	}ge;
	return "Pig latin: " . lc($_);
}


sub letterlong {
	my $text = $_[0];
	my $length = length($text);
	my $start = uc(substr($text,0,1));
	my $end = uc(substr($text,$length-1,1));
	local $_ = $text;
	$spaces = tr/ //;
	$length -= $spaces;
	if ($spaces) {
		$spaces++;
		$length = "$spaces words, $length";
	}
	return "$length letters long. Starts with '$start' and ends with '$end'.";
}

sub vowelcount {
	my $text = $_[0];
	my $length = length($text);
	local $_ = $text;
	$cnt = tr/AEIOUaeiou//;
	$spaces = tr/ //;
	$length -= $spaces;
	if ($spaces) {
		$spaces++;
		$length = "$spaces words, $length";
	}
	return "$length letters long and contains $cnt vowels.";
}


sub NumberToName {
	my $nstr = shift || 0;
	$F = 0;
	foreach my $key (@numberstrs) {
		my ($high,$val) = split(' ',$key,2);
		if ($high == $nstr) {
			return $val;
		}
	}
	return $high;
}


sub GetNearestNumberStr {
	my $nstr = shift || 0;
	$F = 0;
	foreach my $key (@numberstrs) {
		my ($high,$val) = split(' ',$key,2);
		if ($high <= $nstr) {
			return $val;
		}
	}
	return "0";
}

sub GetNearestNumberVal {
	my $nstr = shift || 0;
	$F = 0;
	foreach my $key (@numberstrs) {
		my ($high,$val) = split(' ',$key,2);
		if ($high <= $nstr) {
			return $high;
		}
	}
	return "0";
}

sub levenshtein
{
    # $s1 and $s2 are the two strings
    # $len1 and $len2 are their respective lengths
    #
    my ($s1, $s2) = @_;
    $s1 = lc($s1);
    $s2 = lc($s2);
    my ($len1, $len2) = (length $s1, length $s2);

    # If one of the strings is empty, the distance is the length
    # of the other string
    #
    return $len2 if ($len1 == 0);
    return $len1 if ($len2 == 0);

    my %mat;

    # Init the distance matrix
    #
    # The first row to 0..$len1
    # The first column to 0..$len2
    # The rest to 0
    #
    # The first row and column are initialized so to denote distance
    # from the empty string
    #
    for (my $i = 0; $i <= $len1; ++$i)
    {
        for (my $j = 0; $j <= $len2; ++$j)
        {
            $mat{$i}{$j} = 0;
            $mat{0}{$j} = $j;
        }

        $mat{$i}{0} = $i;
    }

    # Some char-by-char processing is ahead, so prepare
    # array of chars from the strings
    #
    my @ar1 = split(//, $s1);
    my @ar2 = split(//, $s2);

    for (my $i = 1; $i <= $len1; ++$i)
    {
        for (my $j = 1; $j <= $len2; ++$j)
        {
            # Set the cost to 1 iff the ith char of $s1
            # equals the jth of $s2
            # 
            # Denotes a substitution cost. When the char are equal
            # there is no need to substitute, so the cost is 0
            #
            my $cost = ($ar1[$i-1] eq $ar2[$j-1]) ? 0 : 1;

            # Cell $mat{$i}{$j} equals the minimum of:
            #
            # - The cell immediately above plus 1
            # - The cell immediately to the left plus 1
            # - The cell diagonally above and to the left plus the cost
            #
            # We can either insert a new char, delete a char or
            # substitute an existing char (with an associated cost)
            #
            $mat{$i}{$j} = min([$mat{$i-1}{$j} + 1,
                                $mat{$i}{$j-1} + 1,
                                $mat{$i-1}{$j-1} + $cost]);
        }
    }

    # Finally, the Levenshtein distance equals the rightmost bottom cell
    # of the matrix
    #
    # Note that $mat{$x}{$y} denotes the distance between the substrings
    # 1..$x and 1..$y
    #
    return $mat{$len1}{$len2};
}


# minimal element of a list
#
sub min
{
    my @list = @{$_[0]};
    my $min = $list[0];

    foreach my $i (@list)
    {
        $min = $i if ($i < $min);
    }

    return $min;
}

sub MakeFirstHint {
	my $s = shift || 0;
	my $indollars = shift || 0;
	my $Q = "";
	if ($s > 0) {
		while ((GetNearestNumberStr($s) ne "0") && ($s > 0)) {
			$Q = $Q . GetNearestNumberStr($s) . ", plus ";
			$s -= GetNearestNumberVal($s);
		}
		if ($s > 0) {
			$Q = $Q . NumberToName($s);
		}
		$Q =~ s/\,\splus\s$//;
	}
	if ($Q eq "") {
		return "The lowest non-negative number";
	}
	if ($indollars > 0) {
		return "$Q, in DOLLARS";
	} else {
		return $Q;
	}
}

sub timer_function {
	my $nid = 'chatspike';
	if ($round > $numquestions+1) {
		$state = 6;
		$score = 0;
	}
	if ($state == 1)
	{
		if ($round < $numquestions) {
		# ask question
			if (($round % 10) == 0) {
				# insane round
				@list = fetch_insane_round();
				%insane = ();
				$found = 0;
				$curr_question = shift @list;
				@insane = @list;
				$insane_num = 0;
				foreach my $ans (@list) {
					$ans =~ s/^\s+//gi;
					$ans =~ s/\s+$//gi;
					if (($ans ne "") && ($ans ne "***END***")) {
						my $t = conv_num($ans);
						if ($t > 0) {
							$ans = $t;
						}
						$ans = tidy_num($ans);
						$insane{lc($ans)} = 1;
						$insane_num++;
					}
				}
				$left = $insane_num;
				pm("Question \002$round\002 of \002$real_total\002: [\002Insane round!\002] \002$curr_question\002 (\002$insane_num\002 answers)");
				$state++;
			} else {
				($curr_qid,$curr_question,$curr_answer,$curr_customhint1,$curr_customhint2,$curr_category,$curr_lastasked,$curr_timesasked,$curr_lastcorrect,$curr_recordtime) = fetch_question($shuffle_list[$round-1]);
				if ($curr_question eq "") {
					## try again one more time
					($curr_qid,$curr_question,$curr_answer,$curr_customhint1,$curr_customhint2,$curr_category,$curr_lastasked,$curr_timesasked,$curr_lastcorrect,$curr_recordtime) = fetch_question($shuffle_list[$round-1]);
				}
				if ($curr_question ne "") {
					$asktime = time();
					$curr_answer =~ s/^\s+//gi;
					$curr_answer =~ s/\s+$//gi;
					my $t = conv_num($curr_answer);
					if ($t > 0) {
						$curr_answer = $t;
					}
					$curr_answer = tidy_num($curr_answer);
					if ($curr_customhint1 eq "") {
						$curr_customhint1 = $curr_answer;
						if ($curr_customhint1 =~ /^\d+$/) {
							$curr_customhint1 = MakeFirstHint($curr_customhint1);
						} else {
							if ($curr_customhint1 =~ /^\$(\d+)$/) {
								$curr_customhint1 = MakeFirstHint($1);
							} else {
								my $r = rand(12);
								if ($r <= 4) {
									# leave only capital letters
									$curr_customhint1 =~ tr/a-z13579/*/;
								} elsif (($r >= 5) && ($r <= 8)) {
									$curr_customhint1 = letterlong($curr_customhint1);
								} else {
									$curr_customhint1 = scramble($curr_customhint1);
								}
							}
						}
					}
					if ($curr_customhint2 eq "") {
						$curr_customhint2 = $curr_answer;
						if (($curr_customhint2 =~ /^\d+$/) || ($curr_customhint2 =~ /^\$(\d+)$/)) {
							$curr = '';
							if ($curr_customhint2 =~ /^\$(\d+)$/) {
								$curr_customhint2 = $1;
								$curr = '$';
							}
							my $r = rand(13);
							if (($r < 3) && ($curr_customhint2 <= 10000)) {
								$curr_customhint2 = dec_to_roman($curr_customhint2);
							} elsif ((($r >= 3) && ($r < 6)) || ($curr_customhint2 > 10000)) {
								$curr_customhint2 = sprintf("Hexadecimal: $curr%X", $curr_customhint2);
							} elsif (($r >= 6) && ($r < 10)) {
								$curr_customhint2 = sprintf("Octal: $curr%o", $curr_customhint2);
							} else {
								$curr_customhint2 = sprintf("Binary: $curr%b", $curr_customhint2);
							}
						} else {
							my $r = rand(12);
							if ($r <= 4) {
								# transpose only the vowels
								$curr_customhint2 =~ tr/aeiouAEIOU24680/*/;
							} elsif (($r >= 5) && ($r <= 6)) {
								$curr_customhint2 = vowelcount($curr_customhint2);
							} else {
								$curr_customhint2 = piglatin($curr_customhint2);
							}
						}
					}
					pm("Question \002$round\002 of \002$real_total\002: [Category: \002$curr_category\002] \002$curr_question\002?");
					debug("$round/$real_total: id $curr_qid, asked $curr_timesasked times.");
				} else {
					pm("The server barfed, and i even tried twice! Skipping one question!");
					$round++;
					if ($round <= $real_total) {
						$state = 1;
					} else {
						$state = 6;
					}
					$curr_answer = "aaaaaaaaaa" x 64;
					return;
				}
				$state = 2;
				$score = ($interval == 3 ? 8 : 4);
			}
		} else {
			$score = 0;
			$state = 6;
		}
	}
	elsif ($state == 2)
	{
		if (($round % 10) == 0) {
			my $twointerval = $interval * 2;
			pm("You have \002$twointerval\002 seconds remaining!");
		} else {
			# first hint
			pm("First hint: \002$curr_customhint1\002");
		}
		$state = 3;
		$score = ($interval == 3 ? 4 : 2);
	}
	elsif ($state == 3)
	{
		if (($round % 10) == 0) {
			pm("You have \002$interval\002 seconds remaining!");
		} else {
			# second hint
			pm("Second hint: \002$curr_customhint2\002");
		}
		$state = 4;
		$score = ($interval == 3 ? 2 : 1);
	}
	elsif ($state == 4)
	{
		if (($round % 10) == 0) {
			$round++;
			my $found = $insane_num - $left;
			pm("Time up! \002$found\002 answers were found!");
			$state = ($round > $numquestions+1 ? $state = 6 : $state = 1);
			$score = 0;
		} else {
			# answer not given
			$round++;
			pm("Out of time! The answer was: \002$curr_answer\002");
			$state = ($round > $numquestions+1 ? $state = 6 : $state = 1);
			$score = 0;
			if (($streak > 1) && ($last_to_answer ne "")) {
				pm("\002$last_to_answer\002's streak of \002$streak\002 answers in a row comes to a grinding halt!");
			}
			$curr_answer = "***************************NULLANSWER*************************+++++++++++++++++" x 32;
			$last_to_answer = "";
			$streak = 1;
			if ($round <= $real_total) {
				pm("A little time to rest your fingers... Next question coming up in \002$interval\002 seconds!");
			}
		}
	}
	elsif ($state == 5)
	{
		# answer correct
		if ($round <= $real_total) {
			pm("A little time to rest your fingers... Next question coming up in \002$interval\002 seconds!");
			$state = 1;
		} else {
			$state = 6;
		}
		$score = 0;
	}
	elsif ($state == 6)
	{
		# end of round
		pm("End of the round of \002$real_total\002 questions!");
		showstats();
		$state = 0;
		$score = 0;
	}
}


#user joined
sub on_join {
	my ($self,$nid,$server,$nick,$ident,$host,$channel) = @_;
	my $x = get_total_questions();
	main::send_notice($nid,$nick,"Welcome to \002#Trivia\002, \002$nick\002! I am your pie-like host, \002NoMorePie\002, running Fruitloopy-Trivia 2.0 with a total of \002$x\002 questions!");
	my $team = get_current_team($nick);
	if ((defined $team) && ($team ne "") && ($team ne "!NOTEAM")) {
		$teams{$nick} = $team;
		main::send_notice($nid,$nick,"You are currently a member of team \002$team\002.");
	}
	my $halfop = get_current_halfop();
	if (lc($nick) eq lc($halfop)) {
		pm("Welcome, \002$nick\002, yesterdays \002winner\002!");
		main::add_mode_queue($nid,$channel,"+h",$nick);
	}
	if ($state == 0) {
		main::send_notice($nid,$nick,"The trivia is \002not\002 currently active. To start the trivia, type \002!trivia start 200\002 for a round of two hundred questions, or change the number for less questions.");
	} else {
		my $mode = ($interval == 3 ? "quickfire" : "normal");
		main::send_notice($nid,$nick,"The trivia is currently \002active\002 in \002$mode\002 mode. To answer, just type the answer on channel. Enjoy!");
		if ($mode eq "quickfire") {
			main::send_notice($nid,$nick,"To play at normal speed once quickfire has finished, type \002!trivia start 200\002 to start a round of 200 questions at normal speed.");
		}
	}
}

sub showstats {
	my $nid = 'chatspike';
	my $str = "";
	@topten = get_top_ten();
	my $counter = 1;
	foreach (@topten) {
		my($score,$nick) = split(' ',$_);
		$str .= "[ $counter. \002$nick\002 ($score) ]   ";
		$counter++;
	}
	pm("Trivia top ten: $str");
	pm("Detailed scores and tables are available at: $url");
}


sub on_privmsg {
	my ($self,$nid,$server,$nick,$ident,$host,$target,$msg) = @_;
	$msg =~ s/^\s+//gi;
	$msg =~ s/\s+$//gi;
	my $x = conv_num($msg);
	if ($x > 0) {
		$msg = $x;
	}
	$msg = tidy_num($msg);
	if (($state > 0) && ($state < 5)) {
		if (($round % 10) == 0) {
			# insane round
			if (defined $insane{lc($msg)}) {
				delete $insane{lc($msg)};
				$left--;
				if ($left < 1) {
					pm("\002$nick\002 found the last answer!");
					$round++;
					$state = ($round > $numquestions+1 ? $state = 6 : $state = 1);
					# answer correct
					if ($round <= $real_total) {
						pm("A little time to rest your fingers... Next question coming up in \002$interval\002 seconds!");
						$state = 1;
					} else {
						$state = 6;
					}
				} else {
					pm("\002$nick\002 was correct with \002$msg\002! \002$left\002 answers remaining out of \002$insane_num\002.");
				}
				update_score_only($nick,1);
				if (!defined $teams{$nick}) {
					my $team = get_current_team($nick);
					if ((defined $team) && ($team ne "") && ($team ne "!NOTEAM")) {
						$teams{$nick} = $team;
					}
				}
				if (defined $teams{$nick}) {
					add_team_points($teams{$nick},1,$nick);
				}
				$score = 0;
			}
		} else {
			if ((length($msg) >= length($curr_answer)) && ((lc($msg) eq lc($curr_answer)) || ( ($curr_answer !~  /^\$(\d+)$/) && ($curr_answer !~  /^(\d+)$/) &&  (levenshtein($msg,$curr_answer) < 2) ))) {
				$round++;
				$time_to_answer = time() - $asktime;
				$time_to_answer = floor($time_to_answer);
				my $submit_time = 99999;
				my $pts = ($score > 1 ? "points" : "point");
				pm("Correct \002$nick\002! The answer was \002$curr_answer\002. You gain $score $pts for answering in \002$time_to_answer\002 seconds!");
				if ($time_to_answer < $record_time) {
					pm("\002$nick\002 has broken the record time for this question!");
					$submit_time = $time_to_answer;
				} else {
					$submit_time = $record_time;
				}
				my $newscore = update_score($nick,$submit_time,$curr_qid,$score); 
				pm("\002$nick\002's score is now \002$newscore\002.");
				if (!defined $teams{$nick}) {
					my $team = get_current_team($nick);
					if ((defined $team) && ($team ne "") && ($team ne "!NOTEAM")) {
						$teams{$nick} = $team;
					}
				}
				if (defined $teams{$nick}) {
					add_team_points($teams{$nick},$score,$nick);
					my $newteamscore = get_team_points($teams{$nick});
						if ($newteamscore ne "") {
						pm("Team \002$teams{$nick}\002 also gains $score $pts and is now on \002$newteamscore\002.");
					}
				}
				my ($personalbest,$topstreaker,$bigstreak) = split('/',get_streak($nick));
				if ($last_to_answer eq $nick) {
					$streak++;
					pm("\002$nick\002 is on a streak! \002$streak\002 questions and counting!");
					if ($streak > $personalbest) {
						if ($personalbest ne "") {
							pm("\002$nick\002 just broke their own personal best streak of \002$personalbest\002");
						}
						change_streak($nick,$streak);
					} else {
						pm("\002$nick\002 has a bit of a way to go yet before they beat their own best streak of \002$personalbest\002...");
					}
					if ($streak > $bigstreak) {
						pm("\002$nick\002 just broke the record streak previously set by \002$topstreaker\002!");
					}
				} else {
					if (($streak > 1) && ($last_to_answer ne "")) {
						pm("\002$nick\002 just ended \002$last_to_answer\002's streak of \002$streak\002 answers in a row!");
					}
					$streak = 1;
				}
				$state = ($round > $numquestions+1 ? $state = 6 : $state = 1);
				$last_to_answer = $nick;
				$curr_answer = "***************************NULLANSWER*************************+++++++++++++++++" x 32;
				# answer correct
				if ($round <= $real_total) {
					pm("A little time to rest your fingers... Next question coming up in \002$interval\002 seconds!");
					$state = 1;
				} else {
					$state = 6;
				}

			}
		}
	}
	if ($msg =~ /^\!trivia\s+/i) {
		my (undef,$trivcommand) = split(' ',$msg,2);
		if (($trivcommand =~ /^\!strivia$/i) || ($trivcommand =~ /^!trivia$/i)) {
			pm("Don't you mean \002!trivia start 10\002 ?...");
		} elsif ($trivcommand =~ /^start\s+(\d+)$/i) {
			if ($state == 0) {
				if (($1 < 5) || ($1 > 200)) {
					pm("You can't have less than 5, or more than 200 questions in a \002normal\002 trivia round.");
					return;
				}
				$numquestions = $1;
				$real_total = $numquestions;
				$numquestions++;
				pm("Round of \002$real_total\002 questions started by \002$nick\002!");
				@shuffle_list = fetch_shuffle_list();
				if (scalar @shuffle_list < 50) {
					@shuffle_list = fetch_shuffle_list();
				}
				if (scalar @shuffle_list < 50) {
					pm("Oh dear. I cant talk to the server. Better try again in a minute.");
					$state = 0;
					return;
				}
				$state = 1;
				$round = 1;
				pm("\002First\002 question coming up!");
				$interval = 20;
				main::delete_timer("trivia_timer");
				main::create_timer("trivia_timer",$self,"timer_function",$interval);
				debug("$nick started round of $real_total questions");
			} else {
				pm("Buhhh... the round is already running, \002$nick\002!");
			}
		} elsif ($trivcommand =~ /^quickfire\s+(\d+)$/i) {
			if ($state == 0) {
				if (($1 < 3) || ($1 > 15)) {
					pm("You cant have this many questions in a \002quickfire\002 round!");
					$state = 0;
					return;
				}
				if (time() < $next_quickfire) {
					pm("Sorry, but you can only play one quickfire round every 30 minutes, otherwise you'd probably run us out of questions before too long!");
					$state = 0;
					return;
				}
				$numquestions = $1;
				$real_total = $numquestions;
				$numquestions++;
				pm("\002QUICKFIRE\002 round of \002$real_total\002 questions started by \002$nick\002!");
				pm("During \002QUICKFIRE\002 round, scoring is \002DOUBLED\002!");
				@shuffle_list = fetch_shuffle_list();
				if (scalar @shuffle_list < 50) {
					@shuffle_list = fetch_shuffle_list();
				}
				if (scalar @shuffle_list < 50) {
					pm("Oh dear. I cant talk to the server. Better try again in a minute.");
					$state = 0;
					return;
				}
				$state = 1;
				$round = 1;
				pm("\002First\002 question coming up \002SOONER THAN YOU THINK\002!");
				$interval = 3;
				main::delete_timer("trivia_timer");
				main::create_timer("trivia_timer",$self,"timer_function",$interval);
				$next_quickfire = time() + (60*30);
				debug("$nick started quickfire round of $real_total questions");
			} else {
				pm("Buhhh... the round is already running, \002$nick\002!");
			}
		} elsif ($trivcommand =~ /^start$/i) {
			pm("You need to specify a number of questions, for example: \002!trivia start 10\002");
		} elsif ($trivcommand =~ /^stop$/i) {
			if ($state == 0) {
				pm("The trivia is not running, \002$nick\002");
				return;
			}
			if ((main::has_ops($nid,$nick,'#trivia')) || (main::has_halfops($nid,$nick,'#trivia'))) {
				pm("Stopping trivia at request of \002$nick\002.");
				showstats();
				$state = 0;
			} else {
				pm("Only an op or halfop may stop a round of trivia.");
			}
		} elsif ($trivcommand =~ /^stats$/i) {
			pm("\002Current trivia stats\002");
			showstats();
		} elsif ($trivcommand =~ /^leave$/i) {
			if (defined $teams{$nick}) {
				leave_team($nick);
				pm("\002$nick\002 has left team \002$teams{$nick}\002");
				delete $teams{$nick};
			} else {
				pm("You are not on any team, \002$nick\002!");
			}
		} elsif ($trivcommand =~ /^rank$/i) {
			pm(get_rank($nick));
		} elsif ($trivcommand =~ /^join\s+(.+)$/i) {
			my $teamtojoin = $1;
			my $rv = join_team($nick,$teamtojoin);
			if ($rv eq "__OK__") {
				$teams{$nick} = $teamtojoin;
				pm("You have successfully joined the team \002$teamtojoin\002, \002$nick\002.");
			} else {
				pm("I cannot bring about world peace, make you a sandwich, or join that team, \002$nick\002.");
			}
		} elsif ($trivcommand =~ /^create\s+(.+)$/i) {
			my $newteam = $1;
			if ($newteam =~ /^\w+$/) {
				if (create_new_team($newteam) eq "__OK__") {
					join_team($nick,$newteam);
					$teams{$nick} = $newteam;
					pm("You have successfully created the team \002$newteam\002 and been joined to it, \002$nick\002");
				} else {
					pm("I couldn't create that team, \002$nick\002.");
				}
			} else {
				pm("The team name \002$newteam\002 is invalid, \002$nick\002. Try again without spaces or funny characters.");
			}
		} else {
			pm("I dont know what the trivia command '\002$trivcommand\002' means.");
		}
	}
}

# returns the total number of questions

sub get_total_questions {
	my $a = 0;
	chomp($a = fetch_page("?opt=total"));
	return $a;
}

# returns the top ten table

sub get_top_ten {
	my $a = "";
	chomp($a = fetch_page("?opt=table"));
	return split('\n',$a);
}

# returns the average score of the channel

sub get_score_average {
	my $a = "";
	chomp($a = fetch_page("?opt=scoreavg"));
	return $a;
}

# returns the name of the current daywinner

sub get_current_halfop {
	my $a = "";
	chomp($a = fetch_page("?opt=currenthalfop"));
	return $a;
}

# returns the team a user is on

sub get_current_team {
	my $a = "";
	my $user = $_[0];
	chomp($a = fetch_page("?opt=currentteam&nick=$user"));
	return $a;
}

# leaves a team

sub leave_team {
	my $a = "";
	my $user = $_[0];
	chomp($a = fetch_page("?opt=leaveteam&nick=$user"));
	return $a;
}

# joins a team

sub join_team {
	my $a = "";
	my $user = $_[0];
	my $team = $_[1];
	chomp($a = fetch_page("?opt=jointeam&name=$team"));
	if ($a eq "__OK__") {
		fetch_page("?opt=setteam&nick=$user&team=$team");
	}
	return $a;
}

# returns printable rank string

sub get_rank {
	my $a = "";
	my $user = $_[0];
	chomp($a = fetch_page("?opt=rank&nick=$user"));
	return $a;
}

# changes a users personal streak updating tables if neccessary

sub change_streak {
	my $a = "";
	my $user = $_[0];
	my $streak = $_[1];
	chomp($a = fetch_page("?opt=changestreak&nick=$user&score=$streak"));
	return $a;
}

# gets users streak scores and all time best

sub get_streak {
	my $a = "";
	my $user = $_[0];
	chomp($a = fetch_page("?opt=getstreak&nick=$user"));
	return $a;
}

sub create_new_team {
	my $a = "";
	my $team = $_[0];
	chomp($a = fetch_page("?opt=createteam&name=$team"));
	return $a;
}

sub check_team_exists {
	my $a = "";
	my $team = $_[0];
	chomp($a = fetch_page("?opt=jointeam&name=$team"));
	return $a;
}

# next: addteampoints
sub add_team_points {
	my $a = "";
	my $team = $_[0];
	my $points = $_[1];
	my $nick = $_[2];
	chomp($a = fetch_page("?opt=addteampoints&name=$team&score=$points&nick=$nick"));
	return $a;
}

sub get_team_points {
	my $a = "";
	my $team = $_[0];
	chomp($a = fetch_page("?opt=getteampoints&name=$team"));
	return $a;
}

# update score: takes nick, record time, id, and score (value of this answer)
# returns users new current score
sub update_score {
	my $a = "";
	my $nick = $_[0];
	my $rt = $_[1];
	my $id = $_[2];
	my $score = $_[3];
	chomp($a = fetch_page("?opt=score&nick=$nick&recordtime=$rt&id=$id&score=$score"));
	return $a;
}

# same as above but score only - used in insane rounds etc
sub update_score_only {
	my $a = "";
	my $nick = $_[0];
	my $score = $_[1];
	chomp($a = fetch_page("?opt=scoreonly&nick=$nick&score=$score"));
	return $a;
}

sub disable_category {
	my $a = "";
	my $cat = $_[0];
	chomp($a = fetch_page("?opt=disable&catname=$cat"));
	return $a;
}

sub enable_category {
	my $a = "";
	my $cat = $_[0];
	chomp($a = fetch_page("?opt=enable&catname=$cat"));
	return $a;
}

sub enable_all_categories {
	my $a = "";
	$a = fetch_page("?opt=enableall");
	return $a;
}

sub fetch_insane_round {
	my $a = "";
	$a = fetch_page("?opt=insane");
	my @list = split("\n",$a);
	return @list;
}

sub get_disabled_list {
	my $a = "";
	$a = fetch_page("?opt=listdis");
	my @list = split("\n",$a);
	return @list;
}

sub fetch_shuffle_list {
	my $a = "";
	$a = fetch_page("?opt=shuffle");
	my @list = split("\n",$a);
	return @list;
}

sub fetch_question {
	my $a = "";
	my $id = $_[0];
	$a = fetch_page("?id=$id");
	my @list = split("\n",$a);
	return @list;
}

sub fetch_page {
	my $url = $_[0];
	my $ua = new LWP::UserAgent;
	my $content = "";
	$ua->timeout(10);
	$ua->agent("Fruitloopy-Trivia/2.0");
	
	my $req = new HTTP::Request GET => "http://brainbox.cc/trivia_public/$url";
	my $res = $ua->request($req);
	if ($res->is_success) {
		$content= $res->content;
	} else {
		debug("fetch_page: \002" . $res->status_line);
		$content = "";
	}
	return $content;
}

1;


