# Math next to Markdown

Inline $a_1 + a_2 = b_{12}$ and $c*d*e$ and $f\_g$: underscores, asterisks and
backslashes inside math are left alone.

Emphasis around math: *the value $x^2$ is large* and **$y$ is small**.

Display math on its own lines:

$$
\int_0^1 x^2 \, dx = \frac{1}{3}
$$

An aligned environment with a label:

$$
\begin{align}
a &= b + c \label{eq:first} \\
d &< e \& f
\end{align}
$$

A macro: $\newcommand{\R}{\mathbb{R}} f\colon \R \to \R$.

Dollar signs in prose: it costs $5, then $10, or 15$ in total. An escaped \$ sign.

- a list item with $\alpha_i$
  - a nested item with $$\sum_{k=1}^n k = \frac{n(n+1)}{2}$$

> A quotation with $\ell$-isogenies
> and $$\Phi_\ell(X, Y) = 0$$ on the next line.

| $x$ | $x^2$ |
|----:|------:|
| 2   | $4$   |

Code keeps its dollars: `$not math$`.

```
$$ also not math $$
```

Unterminated: $x + y and $$z.
