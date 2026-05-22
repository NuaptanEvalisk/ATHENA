# Namespaces
>[!abstract] Definition **Namespace**
>A **namespace** $N$ in a vault $V$ is an abstract set with a name $|N|$. The name is a legitimate TeXmacs string. A namespace is **semi-concrete** if it designates a filename template $T$ and a sorting algorithm $S$. A namespace is **concrete** if it is semi-concrete and it also designates a file style sheet (file template) $F$.

>[!abstract] Definition **Belonging and Hierarchy**
>If a file $f$ satisfies the filename template $T$ of a semi-concrete namespace $N$, then we say $f$ **belongs to** $N$, i.e. $f\in N$. Namespaces can have hierarchy. If $N'$ is a **subspace** of $N$ we write $N'\subset N$.

Namespaces can be accessed by `tmfs` (TeXmacs virtual pseudo file system) addresses. For $N$, link `tmfs://ns/|N|` points to it, $|N|$ the name of $N$. Link `tmfs://ns/|N|/|N'|` points to subspace $N'\subset N$, which has equivalent functionality of `tmfs://|N'|` except for that if $N'\not\subset N$, then this should produce an error, instead of resolving to $N'$, even if $N'$ is present. By filenames we mean stem filenames.

>[!note] Proposition **If file $f$ satisfies $T_N$ and $N\subset M$, then it must also satisfy $T_M$.**

>[!example] Example
>Let $R$ denote the namespace of course Real Analysis, $L$ denote the namespace of Lecture Notes, $RL$ denote the namespace of Lecture Notes for course Real Analysis, then the following addresses should be legitimate and point to the same thing:
>```
>tmfs://|RL|
>tmfs://|R|/|RL|
>tmfs://|L|/|RL|
>```

>[!note] Remark
>Namespaces provide more representability and flexibility than directory-based file system representation. In a file system, you cannot create a directory $RL$ in *both* directories $R$ and $L$. It also provides more logical structures than the plain tag system in Obsidian.

The naming template $T$ is a function sending every string to either $\top$ or $\bot$. In practice, it should not be fully featured regex, because regex is difficult to write. In fact, it is a C-style `printf` format string with the following formatting primitives allowed:
- `%s` -- arbitrary string
- `%w` -- arbitrary word (no space in-between)
- `%c` -- arbitrary char
- `%d` -- arbitrary integer (0 and <0 allowed)
- `%N` -- arbitrary integer (>0)
- `%R` -- arbitrary Roman number, e.g. XII, IV, L

>[!example] Example
>Here is the naming template for $L$.
>```
>"%w Lecture Notes %R"
>```
>One example is: `DE Lecture Notes XIV`. 

The sorting algorithm $S$ gives a deterministic way to compare two names following template $T$. For different names $\alpha\neq\beta$, it is allowed that $S(\alpha)=S(\beta)$.

>[!note] Proposition **Every two filenames following $T$ are comparable. Thus $S$ values in a totally ordered set. Furthermore, we may always assume $S$ values in $\mathbb Z$.**

>[!abstract] Definition **Completeness of Sorting Algorithms**
>We say sorting algorithm is **complete**, if for different names $\alpha\neq\beta$ following $T$, either $S(\alpha)>S(\beta)$ or $S(\alpha)<S(\beta)$.

We *require* the following statement to be true. It can be thought as an axiom.

>[!note] Proposition **(Compatibility of Sorting Algorithms for Hierarchical Namespaces) Let $N'\subset N$ be semi-concrete namespaces, equipped with sorting algorithms $S$ and $S'$ respectively. Then for files $f,g\in N'$, $S'(f)\geq S'(g)$ iff $S(f)\geq S(g)$. If $S(f)>S(g)$ strictly, $S'(f)>S'(g)$.**

Note that it is possible that $S'(f)>S'(g)$ while $S(f)=S(g)$. However, $S'(f)=S'(g)$ while $S(f)>S(g)$ is not possible. 

>[!note] Corollary **Let $N'\subset N$ be semi-concrete namespaces. If sorting algorithm $S$ of $N$ is complete, then it restricts to the sorting algorithm of $N'$.**

>[!example] Example
>A natural sorting algorithm for the namespace $L$ defined above (with $T$ being `%w Lecture Notes %R`) is the following. For names $\alpha$, $\beta$, let the values for the `%w` fields be $w_1$, $w_2$ and values for the `%R` fields be $R_1$, $R_2$. Then if $w_1\neq w_2$, $S_L(\alpha)=S_L(\beta)$, if $w_1=w_2$, then the comparison result is the same as comparison of Roman numbers $R_1$ and $R_2$.

>[!abstract] Definition **Derivation**
>We say a semi-concrete subspace $N'$ of a semi-concrete namespace $N$ is **derived** from $N$, if its naming template $T_{N'}$ is defined by filling in fields of naming template $T_N$ of $N$.

>[!example] Example
>By filling in the `%w` of `%w Lecture Notes %R` as `"REAL"`, we obtain `REAL Lecture Notes %R`, which is the naming template of $RL$. Thus $RL$ is derived from $L$. It is also derived from $R$, because $T_R$ is `REAL %s %R`.

One derivation is allowed to fill multiple fields.

>[!example] Example
>Vault $V$ has a natural semi-concrete namespace structure: $T_V$ is `%s`, which matches every file, and the sorting algorithm is trivial, i.e. it equalizes all files. This is called the *universal namespace* because any namespace over $V$ is a subspace of it. Every file $f$ induces a *discrete namespace* $D_f$ whose naming template $T_f$ is the filename of $f$. 

>[!abstract] Definition **Compatible Sorting Algorithms**
>Let $N$ and $N'$ be semi-concrete namespaces with sorting algorithms $S$ and $S'$. These two algorithms are **compatible** if there does not exist files $f_1,f_2\in N\cap N'$ such that $S(f_1)>S(f_2)$ and $S'(f_1)<S'(f_2)$ simultaneously.

>[!note] Corollary **If semi-concrete namespaces are disjoint, i.e. $N\cap N'=\emptyset$, then their sorting algorithms are compatible.**

>[!abstract] Definition **Product Sorting Algorithm**
>Let $N$ and $N'$ be namespaces. If neither of them is semi-concrete, then then a trivial sorting algorithm $\widetilde S$ can be defined on $N\cap N'$. If exactly one of them is semi-concrete, its sorting algorithm restricts to a sorting algorithm $\widetilde S$ on $N\cap N'$. If they are both semi-concrete and their sorting algorithms are compatible, for $f_1,f_2\in N\cap N'$, we define $\widetilde S(f_1)>\widetilde S(f_2)$ if $S(f_1)>S(f_2)$ or $S'(f_1)>S'(f_2)$; $\widetilde S(f_1)<\widetilde S(f_2)$ if $S(f_1)<S(f_2)$ or $S'(f_1)<S'(f_2)$, and $\widetilde S(f_1)=\widetilde S(f_2)$ if otherwise. The sorting algorithm $\widetilde S$ defined this way is called the **product** of $S$ and $S'$ and we write $\widetilde S=S\cdot S'$.

>[!example] Example
>Let $N'\subset N$ be semi-concrete namespaces. Then $\widetilde S$ is equal to $S'$, the sorting algorithm of $N'$.

>[!example] Example
>Let $R$ have trivial sorting algorithm, and a sorting algorithm for $L$ is defined in an example above. Then their product sorting algorithm is a sorting algorithm for $RL$, and compares the `%R` Roman number in the filenames of lecture notes for course Real Analysis.

>[!abstract] Definition **Sub-Product Namespace**
>Let $N$, $M$ be namespaces. A namespace $Y$ is a **sub-product** of $N$ and $M$ if $Y=N\cap M$ set-theoretically, is derived from $N$ if $N$ is semi-concrete and is derived from $M$ if $M$ is semi-concrete, and has its sorting algorithm being the product of sorting algorithms of $N$ and $M$.

>[!example] Example
>Namespace $RL$ is a sub-product of $R$ and $L$. However, $R$ is not a sub-product of $R$ and $L$.

Now we describe how a sorting algorithm works in action. Suppose there is a naming template
```
Text file %w %N%N %R ABC
```
Then there are four fields, $x_1,x_2,x_3,x_4$ being the four fields respectively, from left to right. The sorting algorithm $S$ is a function
$$
S:\left(\prod_{1\leq i\leq 4}X_i\right)^2\longrightarrow\left\{0,\pm1\right\}
$$
User will give ATHENA their sorting algorithm as a C function, and ATHENA will access it via `libtcc`. 

As a starting point, we will implement
- data structures for namespaces
- infrastructure for sorting algorithms, including comparison helper functions for Roman numbers and lexicographic comparing for strings, which user's sorting algorithm can use
- `libtcc` integration and loading of custom sorting algorithm
- CRUD of namespaces' registration. Including a `Namespace Manager` pane.
- First application of namespaces: their `tmfs` link will point to a `Namespace Info` page which currently only shows basic info of the pointed namespace.