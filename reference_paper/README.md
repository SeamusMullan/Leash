# Reference papers

Leash's design is grounded in three papers. The PDFs are **not** redistributed
in this repository, because their copyright is held by their authors and
publishers rather than by this project. Fetch them from arXiv:

| arXiv | Paper |
|---|---|
| [2605.13357](https://arxiv.org/abs/2605.13357) | Zhong & Zhu, *AI Harness Engineering: A Runtime Substrate for Foundation-Model Software Agents* |
| [2605.18747](https://arxiv.org/abs/2605.18747) | Ning et al., *Code as Agent Harness: Toward Executable, Verifiable, and Stateful Agent Systems* |
| [2605.27276](https://arxiv.org/abs/2605.27276) | Hebbar et al., *SIA: Self-Improving AI with Harness & Weight Updates* |

```sh
for id in 2605.13357v1 2605.18747v1 2605.27276v2; do
  curl -Lo "reference_paper/$id.pdf" "https://arxiv.org/pdf/$id"
done
```

Everything the design actually depends on is summarised in
`docs/research/paper-notes.md`, which maps each decision back to its source, and
`docs/research/digest-code-as-agent-harness.md`. Reading those is enough to work
on Leash; the PDFs are for checking a claim against the original.
