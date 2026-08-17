---
name: openglad-idea-mining
description: Transcribing audio recordings (playtest sessions, design brainstorms, voice memos — often Russian, chaotic, multi-speaker) and mining them for gameplay bugs, level directions, and enhancement requests, then turning the survivors into filed GitHub issues. Use whenever a task mentions audio in ideas/, voice recordings, transcribing, "mine the recordings", or the drafting/filing stage of any recording-sourced issue batch.
---

# Idea mining: recordings → transcripts → issues

Distilled from the session that turned three family recordings into issues
#218–#226. The pipeline is: triage → multi-engine transcription → code
grounding → mined lists → user correction → root-cause research → drafts →
user review → filing. Each arrow is a stop where the output can change; do
not collapse them.

## 1. Triage the audio

- `ffprobe` every file for duration. Voice-memo FLACs may report
  `Duration: N/A` — decode with `ffmpeg -i f.flac -f null -` and read the
  final `time=`.
- Expect the worst: Russian with English tech terms mixed in, several
  speakers including kids, live game audio underneath. One clean
  transcription pass will not happen; plan for cross-checking from the start.
- Classify each file before mining — it changes how you read the transcript:
  - **Live playtest**: bugs called out mid-game between gameplay chatter.
    Dense, fragmented, highest value per minute.
  - **Design brainstorm**: one idea negotiated between speakers; the final
    agreed shape matters, not the opening pitch.
  - **One-breath memo**: a single idea, often elliptical; expect to present
    2–3 candidate interpretations rather than one confident reading.

## 2. Transcribe with multiple engines, hosted first

Sending audio to a hosted API is publishing it — confirm with the user
unless consent is already on record (check session memory). API keys live in
the user's local secrets env file (see session memory for where); `source`
it in the shell and never echo, log, or commit key material.

Hosted (OpenAI) is faster and measurably better on chaotic speech. Run all
three per file — they fail differently:

- `whisper-1` with `response_format=verbose_json` — segment timestamps,
  needed for citing findings.
- `gpt-4o-transcribe` — best raw accuracy on mixed-language speech.
- `gpt-4o-transcribe-diarize` with `response_format=diarized_json` **and**
  `-F 'chunking_strategy=auto'` — omitting `chunking_strategy` is a 400
  ("chunking_strategy is required for diarization models"). Speaker turns
  are gold for multi-speaker brainstorms.

```bash
curl -s https://api.openai.com/v1/audio/transcriptions \
  -H "Authorization: Bearer $KEY" \
  -F "file=@rec.m4a" -F model=whisper-1 -F language=ru \
  -F response_format=verbose_json
```

m4a/flac upload directly (25 MB/file limit); no conversion needed.

Local fallback (no key or no consent): `faster-whisper` large-v3, int8 on
CPU, `language=<lang>`, `vad_filter=True`,
`condition_on_previous_text=False` (prevents repetition loops on chaotic
audio). Roughly real-time on 8 cores. Machine gotcha: under a nix `python3`,
pip's native wheels (PyAV, ctranslate2, vosk) fail on missing `libz.so.1` /
`libstdc++.so.6` — prefix runs with
`LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu`. Vosk small models are a sanity
check only — too garbled to mine alone. Local engines need
`ffmpeg -ac 1 -ar 16000` WAV.

**Cross-read at least two independent engines before quoting anything.**
Real example: local large-v3 heard "TeamSphere" and "телетон"; whisper-1
recovered "Teams Fair" and "скелетон" — the difference between an
unactionable note and two filed bugs (#218, #221).

## 3. Ground every game term in the code

The transcript is evidence; the tree is truth. Before writing a finding
down, grep the codebase for candidate spellings of each game term the
speakers used ("Teams Fair" → the `TROOPS: FAIR` sentinel in
`campaigns/modes/packs/modes.core/`). The same grep usually locates the
subsystem the finding lives in — record the file paths with the finding.

## 4. Mine into three lists, then stop

Produce exactly: **gameplay bugs**, **directions for new levels**,
**directions for new features**. Every item carries its timestamp and source
recording. Ambiguous items get the candidate interpretations, not a forced
pick.

Report the lists and **stop — do not draft issues yet.** Mining output is a
conversation, not a deliverable: in the reference session the user
reinterpreted a "new level" item as a viewscreen mechanic, merged two bug
hypotheses into one root cause, and redirected a design's entire framing.
Every correction happened at this checkpoint, where it was cheap.

## 5. Root-cause before drafting

- Fan out parallel Explore agents, one per implicated subsystem. Every
  mechanism claim in a draft cites `file:line`.
- The playtest hypothesis is a starting question, not issue text. "We hit a
  nonexistent team's goal so it didn't count" was reasonable and wrong — the
  actual cause was roster activation silently discarding the lobby team
  count, plus painted-but-inactive goals. Research both confirms and
  redirects.
- Research turns up adjacent latent bugs (a team-cycle handler mutating
  state before its null check). Put them in the same issue as the path they
  share, flagged as found during research — not in new issues nobody asked
  for.

## 6. Drafting rules (each learned from user feedback)

- Draft to an untracked scratch file (`ideas/issue-drafts.md`); never file
  in the same turn as drafting.
- English only. Paraphrase foreign-language quotes; never paste transcript
  Cyrillic into an issue.
- State what a design **is**; delete any sentence that argues with a
  framing nobody will see ("It is not a re-themed X").
- When the user hands you a simpler fix model, that model becomes the fix
  section's headline, and the mechanism details serve it — not the other
  way around.
- An application of a feature belongs inside the enabling feature's issue
  (as a "first application" section), not as its own issue.
- Run the edit-writing pass over the whole file before showing it. The
  usual finds in issue prose: bold-for-emphasis overuse, echo restatements,
  reveal colons, sloganish header parentheticals.
- After any restructure (merge, renumber, rescope), re-grep every internal
  "issue #N" reference — they go stale silently, and one stale ref survived
  three review rounds in the reference session.

## 7. Filing (only when told)

- First line of every issue body:
  `THIS MESSAGE WAS GENERATED BY AN AUTOMATED PROCESS`
- `gh label list` first; apply only labels that exist in the repo, strip
  the draft's `**Labels:**` line from the body.
- Draft numbers are not issue numbers. Two-pass filing: create every issue
  with placeholder cross-refs, collect the real numbers, then
  `gh issue edit` the placeholders. The regex will miss line-wrapped refs
  (`issue\n1`) — after filing, read every body back and verify each
  cross-ref resolved. Do not trust the pass.
- Report the mapping (draft → issue # → URL) and leave the drafts file
  matching what was filed, as the local record.
