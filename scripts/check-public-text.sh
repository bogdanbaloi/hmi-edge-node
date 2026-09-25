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

# The shapes live in a data file next to this script, never in the script.
#
# Two reasons, both paid for. The first version of this file spelled the
# forbidden sequences inline and tripped its own check. And on 2026-09-25 the
# three checkers on this machine turned out to ban three different sets, with
# the en dash missing from this one, so the local guard refused a shape that
# this job then accepted. Hub now keeps a shared list read by the other two,
# and asked for this list as DATA rather than as code, so the two can be
# compared without either side parsing the other's script. A fact derived from
# code breaks at the first refactor.
#
# A missing file is a hard failure rather than a fallback. This job runs on a
# runner with the repo checked out, so the file is either there or something
# is wrong with the checkout, and a built in list would quietly check less
# than it claims.
SHAPES="$(dirname "$0")/public-text-shapes.conf"
if [ ! -f "$SHAPES" ]; then
    echo "check-public-text: cannot read $SHAPES" >&2
    exit 2
fi

# Field 3 of each row, trimmed, then unquoted. The spaces INSIDE the quotes
# are part of the shape, which is why the trim happens before the quotes come
# off. Then every regular expression metacharacter is escaped, because the
# file holds plain literals by design and one of them contains a bracket.
FORBIDDEN=$(awk -F"|" '
    !/^[[:space:]]*#/ && NF >= 4 {
        s = $3
        gsub(/^[[:space:]]+|[[:space:]]+$/, "", s)
        gsub(/^"|"$/, "", s)
        if (s != "") print s
    }' "$SHAPES" |
    sed 's/[][\.^$*+?(){}|]/\\&/g' |
    paste -sd"|" -)

if [ -z "$FORBIDDEN" ]; then
    echo "check-public-text: no shapes read from $SHAPES" >&2
    exit 2
fi

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

# The rule is about the SENTENCE, not about where the text happens to wrap,
# so the same text is checked again with every line break turned into a
# space.
#
# Found on 2026-09-25, by this file passing a commit of mine that broke the
# rule twice. A body wraps near 72 characters, so a comma before and landed
# once at the end of a line, where the pattern wants a space it cannot
# find, and once with the comma on one line and the and on the next. GitHub
# then used that same message as the pull request description, on one long
# line, where CI refused it. That is the only reason anybody noticed.
#
# The folded text has no line numbers left, so the report shows the
# surrounding words instead, which is what a reader needs to find it.
folded=$(printf '%s\n' "$text" | tr '\n' ' ' \
         | grep -oE ".{0,45}(${FORBIDDEN}).{0,45}" || true)

if [ -n "$found" ] || [ -n "$folded" ]; then
    echo "Public text breaks the house rules, in $what:"
    [ -n "$found" ] && printf '%s\n' "$found"
    if [ -n "$folded" ]; then
        echo "Reading it as sentences, with the line breaks removed:"
        printf '%s\n' "$folded" | sed 's/^/  ... /'
    fi
    echo ""
    echo "Not allowed: a semicolon, a comma before and, an em-dash, a spaced"
    echo "double hyphen, or an attribution trailer."
    echo "Use a full stop, a comma, a colon, or two sentences."
    exit 1
fi

echo "Public text is clean: $what"
