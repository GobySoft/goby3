# Structural check of goby interface's D2 output, for when d2 itself is not installed: braces
# balance, every line's quotes are closed, tooltips have no raw < or >, and both ends of every
# connection are declared.
function path_to(id,    p, i) {
    p = ""
    for (i = 1; i <= depth; ++i) p = p stack[i] "."
    return p id
}
/^classes: \{$/ { in_classes = 1; next }
in_classes { if ($0 ~ /^\}$/) in_classes = 0; next }
{
    line = $0; gsub(/\\./, "", line); quotes = gsub(/"/, "", line)
    if (quotes % 2) { print FILENAME ":" NR ": unclosed quote"; bad = 1 }
}
# d2 renders tooltips as Markdown, so an unescaped <...> (a template argument list) is read as a tag
match($0, /tooltip: "([^"\\]|\\.)*"/) {
    if (substr($0, RSTART, RLENGTH) ~ /[<>]/) { print FILENAME ":" NR ": unescaped < or > in a tooltip"; bad = 1 }
}
/ -> / {
    split($0, ends, " -> "); to = ends[2]; sub(/: ".*$/, "", to)
    edge[++edges] = ends[1]; edge_line[edges] = NR
    edge[++edges] = to; edge_line[edges] = NR
    next
}
/^ *[A-Za-z0-9_]+: \{$/ { id = $1; sub(/:$/, "", id); declared[path_to(id)] = 1; stack[++depth] = id; next }
/^ *[A-Za-z0-9_]+: \{ .* \}$/ { id = $1; sub(/:$/, "", id); declared[path_to(id)] = 1; next }
/^ *\}$/ { if (--depth < 0) { print FILENAME ":" NR ": unbalanced }"; bad = 1; depth = 0 }; next }
END {
    if (depth != 0) { print FILENAME ": " depth " unclosed {"; bad = 1 }
    for (i = 1; i <= edges; ++i)
        if (!(edge[i] in declared)) { print FILENAME ":" edge_line[i] ": " edge[i] " is not declared"; bad = 1 }
    exit bad
}
