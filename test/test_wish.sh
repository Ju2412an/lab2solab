#!/bin/bash
# Batería de pruebas automatizadas para wish.
# Ejecutar desde el directorio dev/ con: bash test/test_wish.sh

set -u
WISH=./wish
PASS=0
FAIL=0
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

run_test() {
    local nombre="$1"
    local esperado="$2"
    local obtenido="$3"
    if [ "$esperado" = "$obtenido" ]; then
        echo "PASA: $nombre"
        PASS=$((PASS + 1))
    else
        echo "FALLA: $nombre"
        echo "  esperado: $(printf '%s' "$esperado" | head -c 200)"
        echo "  obtenido: $(printf '%s' "$obtenido" | head -c 200)"
        FAIL=$((FAIL + 1))
    fi
}

# 1. Comando simple ls (debe encontrarlo en /bin)
out=$(echo "ls /" | $WISH 2>/dev/null | sort | head -n 1)
[ -n "$out" ] && run_test "1. ls / imprime contenido" "bin" "$(echo "ls /" | $WISH 2>/dev/null | grep -x bin)"

# 2. exit con argumento extra -> error
out=$(echo "exit extra" | $WISH 2>&1 >/dev/null)
run_test "2. exit con argumento extra imprime error" "An error has occurred" "$out"

# 3. cd sin argumento -> error
out=$(echo "cd" | $WISH 2>&1 >/dev/null)
run_test "3. cd sin argumento imprime error" "An error has occurred" "$out"

# 4. cd con más de un argumento -> error
out=$(echo "cd /tmp /var" | $WISH 2>&1 >/dev/null)
run_test "4. cd con dos argumentos imprime error" "An error has occurred" "$out"

# 5. path vacío: no se puede ejecutar ls
out=$(printf "path\nls\n" | $WISH 2>&1 >/dev/null)
run_test "5. path vacío impide ejecutar ls" "An error has occurred" "$out"

# 6. path /usr/bin permite ejecutar echo de /usr/bin
out=$(printf "path /usr/bin\necho hola\n" | $WISH 2>/dev/null | sed 's/wish> //g' | grep -v '^$')
run_test "6. path /usr/bin permite ejecutar echo" "hola" "$out"

# 7. Redirección de salida
rm -f "$TMP/out.txt"
printf "ls / > %s\n" "$TMP/out.txt" | $WISH >/dev/null 2>&1
if [ -s "$TMP/out.txt" ]; then
    run_test "7. redirección escribe archivo" "ok" "ok"
else
    run_test "7. redirección escribe archivo" "ok" "fallo"
fi

# 8. Redirección sin archivo -> error
out=$(echo "ls >" | $WISH 2>&1 >/dev/null)
run_test "8. ls > sin archivo imprime error" "An error has occurred" "$out"

# 9. Dos archivos tras > -> error
out=$(echo "ls > a b" | $WISH 2>&1 >/dev/null)
run_test "9. ls > a b imprime error" "An error has occurred" "$out"

# 10. Dos operadores > -> error
out=$(echo "ls > a > b" | $WISH 2>&1 >/dev/null)
run_test "10. ls > a > b imprime error" "An error has occurred" "$out"

# 11. Comandos paralelos (vía batch para evitar prompts)
cat > "$TMP/par.txt" <<EOF
path /bin /usr/bin
echo uno & echo dos
EOF
out=$($WISH "$TMP/par.txt" 2>/dev/null | sort | tr '\n' ' ')
run_test "11. echo uno & echo dos imprime ambos" "dos uno " "$out"

# 12. Comando desconocido -> error
out=$(echo "nocomando" | $WISH 2>&1 >/dev/null)
run_test "12. comando inexistente imprime error" "An error has occurred" "$out"

# 13. Línea vacía no produce error
out=$(printf "\n\n" | $WISH 2>&1 >/dev/null)
run_test "13. líneas en blanco no producen error" "" "$out"

# 14. Modo batch
cat > "$TMP/batch.txt" <<EOF
path /usr/bin /bin
echo batch funciona
EOF
out=$($WISH "$TMP/batch.txt" 2>/dev/null)
run_test "14. modo batch ejecuta comandos" "batch funciona" "$out"

# 15. Más de un argumento al shell -> error + exit 1
$WISH a b 2>/dev/null
run_test "15. wish con >1 argumento sale con 1" "1" "$?"

# 16. Archivo batch inexistente -> error + exit 1
$WISH /archivo/que/no/existe 2>/dev/null
run_test "16. batch inexistente sale con 1" "1" "$?"

# 17. Sin operadores entre > (sin espacios)
rm -f "$TMP/out2.txt"
printf "ls />%s\n" "$TMP/out2.txt" | $WISH >/dev/null 2>&1
if [ -s "$TMP/out2.txt" ]; then
    run_test "17. redirección sin espacios alrededor de >" "ok" "ok"
else
    run_test "17. redirección sin espacios alrededor de >" "ok" "fallo"
fi

# 18. cd real cambia directorio (verificamos con pwd vía batch)
cat > "$TMP/cd.txt" <<EOF
cd /tmp
path /bin
pwd
EOF
out=$($WISH "$TMP/cd.txt" 2>/dev/null)
run_test "18. cd cambia directorio" "/tmp" "$out"

# 19. chd (nombre que usó el profesor en clase) también cambia directorio
cat > "$TMP/chd.txt" <<EOF
chd /tmp
path /bin
pwd
EOF
out=$($WISH "$TMP/chd.txt" 2>/dev/null)
run_test "19. chd (alias de cd) cambia directorio" "/tmp" "$out"

# 20. route (nombre que usó el profesor en clase) también sobrescribe el path
cat > "$TMP/route.txt" <<EOF
route /usr/bin
echo via route
EOF
out=$($WISH "$TMP/route.txt" 2>/dev/null)
run_test "20. route (alias de path) sobrescribe path" "via route" "$out"

echo
echo "Resultado: $PASS pasaron, $FAIL fallaron"
exit $FAIL
