#!/bin/sh
# Refuses the text this project publishes if it breaks the house rules.
#
# The rules are in workstream-charters RULES.md: no semicolon, no comma before
# and, no em-dash and no spaced double hyphen in commit subjects, commit
# bodies, pull request titles or pull request descriptions, and no attribution
# trailers.
#
# It exists because the rule was broken twice in one day while being followed
# "by discipline": a self-scan before every commit works right up to the
# moment somebody is concentrating on something else. A gate that runs every
# time does not have that failure mode.
#
# Two ways to call it:
#
#   check-public-text.sh <range>   the commits in a git range, subject and body
#   check-public-text.sh -         whatever arrives on standard input
#
# Exits 1 and prints the offending lines when it finds something.

set -e

# Both the em-dash and the spaced double hyphen are BUILT rather than typed,
# so that the file which refuses them does not contain them. The em-dash comes
# from its own UTF-8 bytes, the double hyphen from a character class. This is
# not decoration: the first version of this script tripped its own check.
EM_DASH=$(printf '\342\200\224')
FORBIDDEN=", and |;|${EM_DASH}| [-][-] |Co-Authored-By|Generated with .Claude"

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <git range>|-" >&2
    exit 2
fi

if [ "$1" = "-" ]; then
    text=$(cat)
    what="the text given"
else
    # %s is the subject, %b the body, and a commit with neither is impossible.
    text=$(git log --format='%h %s%n%b' "$1")
    what="the commits in $1"
fi

found=$(printf '%s\n' "$text" | grep -nE "$FORBIDDEN" || true)

if [ -n "$found" ]; then
    echo "Public text breaks the house rules, in $what:"
    printf '%s\n' "$found"
    echo ""
    echo "Not allowed: a semicolon, a comma before and, an em-dash, a spaced"
    echo "double hyphen, or an attribution trailer."
    echo "Use a full stop, a comma, a colon, or two sentences."
    exit 1
fi

echo "Public text is clean: $what"
