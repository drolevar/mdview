# Malformed math

Good inline: $x + y = z$.

Bad inline (unknown command): $\notarealcommand{x}$.

Good display:

$$
\sum_{i=1}^{n} i = \frac{n(n+1)}{2}
$$

Bad display (mismatched braces):

$$
\frac{1}{2
$$

More good after the bad: $\alpha + \beta$.
