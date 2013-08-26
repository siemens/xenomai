BEGIN {
	link_re="\"(http|file|https|ftp|irc|mailto):[^#\"]*$"
}

/Table of Contents/ {
	in_toc=1
	print $0
	next
}

/List of Tables/ {
	in_toc=0
	in_lot=1
	next
}

(in_toc || in_lot) && /^([ \t]*[0-9]\.|$)/ {
	if (in_toc)
		print $0
	next
}

(in_toc || in_lot) && /^[ \t]*$/ {
	if (in_toc)
		print $0
	next
}

in_lot {
	printf "-------------------------------------------------------------------------------\n\n"
	in_lot=0
}

$0 ~ link_re {
	i = match($0, link_re)
	print substr($0, 1, i - 1)
	unfinished_url=substr($0, i)
	next
}

unfinished_url && /"/ {
	sub(/^[ \t]*/,"")
	print unfinished_url$0
	unfinished_url=0
	next
}

unfinished_url {
	sub(/^[ \t]*/,"")
	unfinished_url=unfinished_url$0
	next
}

/^Chapter [0-9]\./ {
	sub(/^Chapter /,"", $0)
	title=$0
	next
}

/^[0-9]\.[0-9.]*/ {
	title=$0
	next
}

title && /^[ \t]*$/ {
	print ""
	print title
	gsub(/./, "-", title)
	print title
	print $0
	title=0
	next
}

title {
	print title
	print $0
	title=0
	next
}

{
	print $0
}
