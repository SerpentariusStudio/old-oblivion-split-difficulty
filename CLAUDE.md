## 1. Think Before Coding

**Don't assume. Don't hide confusion. Surface tradeoffs.**

Before implementing:
- State your assumptions explicitly. If uncertain, ask.
- If multiple interpretations exist, present them - don't pick silently.
- If a simpler approach exists, say so. Push back when warranted.
- If something is unclear, stop. Name what's confusing. Ask.

## 2. Simplicity First

**Minimum code that solves the problem. Nothing speculative.**

- No features beyond what was asked. 
- No abstractions for single-use code.
- No "flexibility" or "configurability" that wasn't requested.
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite it.

Ask youself: "Would a senior engineer say this is overcomplicated?" If Yes, simplify.

## 3. Surgical changes.

**Touch only what you must. Clean up only your own mess.**

When editing existing code:
- Don't "improve" adjacent code, comments, or formatting.
- Don't refactor things that aren't broken.
- Match existing style, even if you'd do it differently.
- If you notice unrelated dead code, mention it - don't delete it.

When your changes create orphans:
- Remove imports/variables/functions that YOUR changes made unused.
- Don't remove pre-existing dead code unless asked.

The test: Every changed line should trace directly to the user's request.

## 4. Goal-Driven Execution

**Define success criteria. Loop until verified.**

Transform tasks into verifiable goals:
- "Add validation" -> "Write tests for invalid inputs, then make them pass"
- "Fix the bug" -> "Write a test that reproduces it, then make it pass"
- "Refactor X" -> "Ensure tests pass before and after"

For multi-step tasks, state a brief plan:
```
1. [Step] -> verify: [check]
2. [Step] -> verify: [check]
3. [Step] -> verify: [check]
```

Strong success criteria let you loop independently. Weak criteria ("make it work") require constant clarification.

---

## Alert
always play a little sound using powershell to alert the user that you are finished with the current task.

## Attempt History (MANDATORY)
Maintain `attempt-history.md` at the repo root. Before trying anything, **read it** so
you don't repeat a failed approach. After every attempt or code change, **append an
entry**: what was tried, why, the result (worked / failed / inconclusive), and the key
takeaway. One concise entry per attempt, newest at the bottom. This applies to RE
findings, plugin code changes, INI/config experiments, and build/runtime outcomes.

## Environment & Constraints
- **Host OS:** Windows 11
- **Shell:** PowerShell only (no bash or cmd examples)
- **NEVER push to remote** (do not run `git push`)

---
