# Testes do módulo Publish/Subscribe

Guia prático para testar o módulo `pubsub` no Buildroot com QEMU.

> **Onde executar:** a seção 1 é no **WSL**. A partir da seção 2, todos os comandos são **dentro do QEMU**.

## 1. Atualizar a imagem e iniciar o QEMU (WSL)

Na raiz do Buildroot:

```sh
cd ~/buildroot-2025.02.16
make -C modules/pubsub
make -C apps/pubsub
make
sh QEMU.sh
```

O Makefile da aplicação instala `pubsub-teste` em `overlay/usr/bin/`.

## 2. Carregar o módulo (QEMU)

```sh
dmesg -n 8
modprobe pubsub max_topics=5 default_max_subscribers=1
ls -l /dev/pubsub
cat /sys/module/pubsub/parameters/max_topics
```

`dmesg -n 8` habilita a exibição de mensagens `pr_info()` no console. Usaremos limite inicial de **1 inscrito por tópico** para testar também a alteração via sysfs.

> Se o módulo já estiver carregado, os parâmetros do `modprobe` não serão reaplicados. Termine os clientes e execute `modprobe -r pubsub` antes de carregá-lo novamente.

## 3. Teste básico: um processo

Execute:

```sh
pubsub-teste
```

No prompt `>` da aplicação, digite **um comando por vez**:

```text
/subscribe teste
/publish teste "Hello World!"
/fetch teste
/read
/read
/unsubscribe teste
/exit
```

**Esperado:** o primeiro `/read` mostra `Received message: Hello World!`; o segundo mostra `No pending messages`. O `/unsubscribe` remove a inscrição e o tópico vazio; `/exit` encerra a aplicação.

## 4. Teste de dois processos (mesmo terminal do QEMU)

### 4.1. Preparar dois clientes

**Fora** da aplicação (no shell `#`), verifique se há clientes antigos:

```sh
ps | grep pubsub-teste
```

Se houver processos `pubsub-teste` antigos, encerre-os antes de continuar (por exemplo, `kill <PID>`). Não deixe instâncias antigas lendo os mesmos FIFOs.

Crie os FIFOs e abra os descritores **antes** de iniciar os clientes:

```sh
rm -f /tmp/cliente1 /tmp/cliente2
mkfifo /tmp/cliente1 /tmp/cliente2
exec 3<>/tmp/cliente1
exec 4<>/tmp/cliente2
pubsub-teste < /tmp/cliente1 &
pubsub-teste < /tmp/cliente2 &
ps | grep pubsub-teste
```

**Esperado:** dois PIDs diferentes. Os clientes **não abrem janelas**: rodam em segundo plano e imprimem no mesmo terminal.

### 4.2. Testar o limite de inscritos

Inscreva o primeiro cliente e tente inscrever o segundo:

```sh
printf '/subscribe teste\n' >&3
sleep 1
printf '/subscribe teste\n' >&4
sleep 1
```

**Esperado:** o primeiro PID é aceito, e o segundo recebe `Maximum subscribers reached for topic 'teste'` (limite inicial = 1).

Aumente o limite pelo sysfs e tente novamente:

```sh
cat /sys/pubsub/teste
echo 2 > /sys/pubsub/teste
cat /sys/pubsub/teste
printf '/subscribe teste\n' >&4
sleep 1
```

**Esperado:** o limite passa a `2` e o segundo PID consegue se inscrever.

### 4.3. Publicar e ler em filas independentes

O cliente 1 publica duas mensagens:

```sh
printf '/publish teste "Mensagem A"\n' >&3
printf '/publish teste "Mensagem B"\n' >&3
sleep 1
```

**Esperado no log:** cada publicação indica `for 2 subscriber(s)`.

Selecione o tópico nos dois clientes e leia primeiro **somente no cliente 1**:

```sh
printf '/fetch teste\n' >&3
printf '/fetch teste\n' >&4
sleep 1
printf '/read\n' >&3
printf '/read\n' >&3
sleep 1
```

**Esperado no cliente 1:** `Mensagem A` e `Mensagem B`.

Agora leia no cliente 2:

```sh
printf '/read\n' >&4
printf '/read\n' >&4
sleep 1
```

**Esperado no cliente 2:** também `Mensagem A` e `Mensagem B`. Isso demonstra que **cada PID possui sua própria fila**.

### 4.4. Testar redução do limite sem remover inscritos

Com os dois clientes ainda inscritos:

```sh
echo 1 > /sys/pubsub/teste
cat /sys/pubsub/teste
printf '/publish teste "Ainda dois"\n' >&3
sleep 1
```

**Esperado:** o arquivo mostra `1`, mas a publicação ainda registra `for 2 subscriber(s)`. Os inscritos existentes são preservados.

## 5. Encerrar e limpar (QEMU)

```sh
printf '/exit\n' >&3
printf '/exit\n' >&4
exec 3>&-
exec 4>&-
wait
rm -f /tmp/cliente1 /tmp/cliente2
modprobe -r pubsub
```

O último inscrito deve ser removido pelo `release()`, os tópicos vazios devem desaparecer e o módulo deve descarregar sem erro.

Para consultar logs a qualquer momento:

```sh
dmesg | tail -30
```

## Checklist rápido

- [ ] `pubsub.ko` carrega e cria `/dev/pubsub`
- [ ] Um processo publica, lê e esvazia sua fila
- [ ] O segundo inscrito é recusado com limite `1`
- [ ] `echo 2 > /sys/pubsub/teste` permite a segunda inscrição
- [ ] Os dois PIDs recebem suas próprias cópias das mensagens
- [ ] Reduzir o limite para `1` não remove os dois inscritos existentes
- [ ] `/exit` e `modprobe -r pubsub` encerram e limpam o sistema
