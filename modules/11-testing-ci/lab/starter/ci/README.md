# Your pipeline goes here

Write `.github/workflows/ci.yml` (or `.gitlab-ci.yml`) for your repository.
It must, on every push and merge request:

1. build and run the host tests in `modules/11-testing-ci/lab/starter` with
   ASan + UBSan (`-DLAB11_SANITIZE=ON`, the default)
2. fail if line coverage of `lib/` is below 90% (`-DLAB11_COVERAGE=ON`, then gcovr)
3. run cppcheck on `lib/` and fail on warnings
4. cross-compile the whole course tree and publish a size report
   (`tools/size_report.py`), with the size change on merge requests

Optional: a hardware-in-the-loop job on a self-hosted runner (see the lab README).
