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
# not decoration: the first version of this script tripped its own check.
#
# The en dash and the two Romanian conjunctions were added on 2026-09-25,
# after putting the three gates side by side: this one, stil.py in the
# commercial repo and rule 7 of bash-guard. All three enforce the same rule
# in RULES.md and no two of them banned the same set. The en dash was
# missing only here, so the shape refused at commit time by the guard was
# accepted by CI, which is the worst direction for a disagreement to run.
EM_DASH=$(printf '\342\200\224')
EN_DASH=$(printf '\342\200\223')
S_HOOK=$(printf '\310\231')
FORBIDDEN=", and |, sau |, ${S_HOOK}i |;|${EM_DASH}|${EN_DASH}| [-][-] |Co-Authored-By|Generated with .Claude"

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
