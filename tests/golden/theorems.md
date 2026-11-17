# Notes on isogeny graphs

These notes show headings with ids, links to them, theorem-like blocks and comments in
math. See [the proof](#a-proof) and [the last section](#remarks-questions).

## Definitions

> **Definition.** Let $\ell \ne p$ be primes. The graph $G_p(\ell)$ has the supersingular
> $j$-invariants over $\overline{\mathbb{F}}_p$ as vertices and $\ell$-isogenies as edges.

> **Theorem (Pizer).** The graph $G_p(\ell)$ is Ramanujan:
> $$
> |\lambda| \le 2\sqrt{\ell} % the nontrivial eigenvalues
> $$
> for every eigenvalue $\lambda \ne \ell + 1$.

> **Lemma.** A walk of length $O(\log p)$ mixes.

## A proof

> **Proof (of the lemma).** Expand in an eigenbasis.
>
> - The trivial eigenvalue gives the uniform distribution.
> - The others decay like $(2\sqrt{\ell}/(\ell+1))^k$.

> **Remark:** an ordinary quotation follows.

> Not a theorem: **Theorem.** appears later in the line.

> **Theorem 7.** A number written by hand is left alone.

> **theorem** in lower case, without a full stop, still counts.

## Comments in math

$$
% a comment on a line of its own
\newcommand{\Hom}{\operatorname{Hom}}
\begin{align}
\Hom(E, E') &\cong I \label{eq:hom} \\ % the connecting ideal
\deg \varphi &= 100\% \cdot n(I)
- 0 \\
\# S &= 1
+ 2
\end{align}
$$

Inline too: $a + b % dropped
  c$, and a percent sign kept: $50\%$.

## A proof

The second heading with this text gets its own id. So does a heading with no letters:

## 2024

### *Remarks* & `questions`

1. [x] a finished task
2. [ ] an open one, [linked](#2024)

Struck ~~through~~ text. Text with LaTeX's special characters: 100% of $5 & #1 {x} a_b x^2 ~ \ < > | "q".
A line that starts with a bracket after a break:  
[bracket] and `code with \ and {braces}`.

```tex
\begin{verbatim}
nested
\end{verbatim}
```
