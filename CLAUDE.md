@AGENTS.md

# Subagents

- Use only a small number of subagents.
- Prefer subagents on smaller models.
- Review what a subagent returns (subagent = junior, you = senior).
- Good uses: research, and uncritical or straightforward implementation work.
- Do not delegate hardware-affecting design decisions — those belong in the FSD and to the user.

# Working style

- Read `docs/FSD.md` before proposing work. It already answers most "what should this do?"
  questions, and it records which decisions are still open.
- When something in the FSD turns out to be wrong or unrealistic once real hardware is involved,
  say so and correct the document — do not quietly implement something different.
- Prefer asking one precise question over building on a guessed assumption, but only when the
  answer actually changes the work. Routine judgement calls are yours to make.
