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
