# HachiCam development workflow

The user requires this order for every bug fix and feature:

1. **Analyze the problem.** Inspect the provided evidence and current behavior;
   distinguish observations from hypotheses.
2. **Analyze the spec.** Read the affected authoritative specifications, identify
   conflicts, missing contracts and differences from the implementation.
3. **Adjust the spec.** Update the relevant specification and acceptance criteria
   before implementing. Distinguish implemented behavior, experimental parameters
   and work that remains unimplemented. Do not use an audit note as a substitute
   for correcting the affected authoritative spec.
4. **Implement the feature.** Make the change against the revised contract, run
   appropriate checks, and report the results and remaining verification limits.

Do not insert an approval gate between these steps unless the action itself
requires authorization. Preserve this workflow for follow-up requests.

## End-of-work handoff

When new evidence identifies actionable work within the already authorized scope,
continue directly from analysis through specification revision, implementation,
verification and the authorized delivery. Do not end the turn after analysis or
wait for another user message merely to start the known next step.

When the user says to wrap up or calls it a day (for example, "收工"), commit the
authorized task changes on the current working branch and push them without
asking again. Verify the remote branch points to the pushed commit before
reporting upload complete. A local commit alone is not an upload.

The user's requests to commit or submit (for example, "提交") also mean commit
and push the authorized changes to GitHub on the current branch by default.
Do not stop after a local commit or ask for push confirmation again. Verify the
live remote branch points to the resulting commit before reporting completion.
