# CLAUDE.md

Guidance for Claude when working in this repository.

## Code comments

- Default to no comments. Well-named identifiers and clear code should speak for themselves.
- Only add a comment when the *why* isn't obvious from the code.
- When a comment is warranted, keep it short: a single line beats a paragraph.
- Don't explain *what* the code does, restate the diff, or reference the current task/fix/caller (e.g. "used by X", "added for the Y flow"). That belongs in the commit message or PR description, not the code.
- Don't overly emphasis historical reasons for the change, assume the current state of the code speaks for itself.
- The exception to these rules is for unit tests - these can be documented with more verbosity when it helps the developers to understand how the test functions and what it tests.

## Documentation

- Keep documentation concise and writtten in a style similar to the rest of the project.
- When in doubt leave the documentation unwritten, but provide an outline for the human developers to use to fill out the rest of the document.
- Prefer figures to text when an idea can be better explained graphically.



