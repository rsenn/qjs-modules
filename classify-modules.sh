#!/bin/bash

# Classify all lib/*.js files into categories

echo "=== Classifying lib/*.js files ==="
echo ""

echo "POLYFILLS (standalone JS, no native imports):"
echo "=============================================="
for file in lib/*.js; do
    name=$(basename "$file" .js)
    # Check if file imports from native modules
    if ! grep -q "from '\(misc\|std\|os\|path\|util\|inspect\|mmap\|syscallerror\|textcode\|blob\|archive\|bcrypt\|bjson\|child-process\|deep\|directory\|gpio\|json\|lexer\|list\|location\|magic\|mysql\|pgsql\|pointer\|predicate\|queue\|repeater\|serial\|sockets\|sqlite\|stream\|tree-walker\|virtual\|xml\|internal\)'" "$file" 2>/dev/null; then
        # Check if there's a corresponding native module
        if [ -f "quickjs-$name.c" ]; then
            echo "  $name (has native quickjs-$name.c but lib/$name.js is polyfill)"
        else
            echo "  $name (pure JS, no native module)"
        fi
    fi
done

echo ""
echo "WRAPPERS (wrap native modules):"
echo "==============================="
for file in lib/*.js; do
    name=$(basename "$file" .js)
    # Check if file imports from native modules
    if grep -q "from '\(misc\|std\|os\|path\|util\|inspect\|mmap\|syscallerror\|textcode\|blob\|archive\|bcrypt\|bjson\|child-process\|deep\|directory\|gpio\|json\|lexer\|list\|location\|magic\|mysql\|pgsql\|pointer\|predicate\|queue\|repeater\|serial\|sockets\|sqlite\|stream\|tree-walker\|virtual\|xml\|internal\)'" "$file" 2>/dev/null; then
        echo "  $name"
    fi
done

echo ""
echo "EXTENSIONS (extend built-in prototypes):"
echo "========================================="
for file in lib/extend*.js; do
    if [ -f "$file" ]; then
        name=$(basename "$file" .js)
        echo "  $name"
    fi
done

echo ""
echo "MODULES WITH BOTH NATIVE AND JS VERSIONS:"
echo "=========================================="
for file in lib/*.js; do
    name=$(basename "$file" .js)
    if [ -f "quickjs-$name.c" ]; then
        echo "  $name (quickjs-$name.c + lib/$name.js)"
    fi
done
