# Changelog

## Unreleased



- S15 permission-sequence harness (journal 0250, validation in progress): the default
  `permchurn` suite step runs seeded chmod, chown, access-ACL, rename, link and unlink
  sequences against the lower kernel as nine identities, including delayed builds.
  `MOOB_PERMCHURN=SEEDS,STEPS[,FIRST]` selects a longer campaign.

