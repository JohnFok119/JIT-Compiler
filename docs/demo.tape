# Terminal clip for the README (docs/media/demo.gif, demo.mp4).
# Render from the repo root:
#   docker build -t jit-vhs -f docs/media/Dockerfile.vhs docs/media
#   docker run --rm -v "$PWD:/vhs" jit-vhs docs/demo.tape
# The jit binary is built inside the container, in a copy of the repo.

Output docs/media/demo.gif
Output docs/media/demo.mp4

Set FontSize 20
Set Width 1200
Set Height 700
Set Padding 24
Set TypingSpeed 45ms
Set Theme { "name": "jit-night", "black": "#14151c", "red": "#e0626a", "green": "#8fcf9a", "yellow": "#d9b865", "blue": "#7f8be0", "magenta": "#b48ad8", "cyan": "#7fc4d4", "white": "#d8d6cc", "brightBlack": "#5b5a6b", "brightRed": "#f07f86", "brightGreen": "#a6e0b0", "brightYellow": "#e8cb85", "brightBlue": "#9aa5f0", "brightMagenta": "#c8a4e8", "brightCyan": "#99d6e4", "brightWhite": "#f4f2e8", "background": "#14151c", "foreground": "#e6e4da", "selection": "#2a2c3d", "cursor": "#c9a449" }

Hide
Type "rm -rf /tmp/s && mkdir /tmp/s && cp -r /vhs/include /vhs/src /vhs/Makefile /tmp/s/ && cd /tmp/s && make -s >/dev/null 2>&1 && export PATH=/tmp/s/build:$PATH && export PS1='$ ' && echo built-$((1+1))"
Enter
Wait+Screen@180s /built-2/
Type "clear"
Enter
Sleep 500ms
Show

Type "jit '2 + 3 * 4'"
Enter
Sleep 1.5s

Type "jit 'sin(x)^2 + cos(x)^2' x=0.7"
Enter
Sleep 1.5s

Type "clear"
Enter
Type "jit --emit asm 'x*x + 3*x'"
Enter
Sleep 3.5s

Type "clear"
Enter
Type "jit --diff x 'x^3 + sin(x)*x'"
Enter
Sleep 2s

Type "jit --bench 1000000 '((3*x - 2)*x + 5)*x - 1'"
Enter
Sleep 3s

Type "jit '(1 + 2'"
Enter
Sleep 3s
