# Independent read-only review

Reviewed application-owned admission/transport and board/native adapters against
the approved design; reviewer did not mutate repository or devices.

Important finding: recover() cleared advertised freshness and inadvertently
bypassed sequence/contact-generation guards. Exact ABC→CBA→recover→old-position
advertisement regression failed, then passed with persistent sequence/generation
watermarks. Reviewer independently reran it and closed the finding.

Native verification later exposed 13.387ms queue-induced clock disagreement.
Added known-offset assertions to all six actual owner/PCM reorder runs (red,
then green). First successful raw submission now stamps only matching group
PING t1/PONG t3; failed submission may restamp, accepted retries and forwarding
are immutable. A regression covers failed first submit plus lost acknowledgements.
Reviewer separately checked old queued PING matching and retry immutability,
reran radio/chain suites, and found no additional Important/Critical issue.

Review does not establish RF/acoustic timing acceptance. Never-observed hidden
participants are the explicitly documented physical observability boundary.
