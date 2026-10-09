# Módulo Publish/Subscribe (`pubsub`)

Módulo carregável do kernel Linux (*Loadable Kernel Module* — LKM) que fornece o dispositivo de caracteres `/dev/pubsub`. Implementa comunicação entre processos por tópicos, utilizando o modelo **Publish/Subscribe**.

> **Pré-requisitos:** o Buildroot já deve estar configurado, a imagem deve conter o módulo `pubsub.ko` e a aplicação `pubsub-teste`, e o QEMU deve estar pronto para iniciar. **Apenas a seção 1 é executada no WSL; todos os comandos das seções seguintes são executados dentro do Linux no QEMU.** Este guia não cobre a compilação nem a configuração inicial do Buildroot.

## 1. Iniciar o QEMU (no WSL)

No terminal do **WSL**, acesse o diretório que contém o script `QEMU.sh` (por exemplo, a raiz do projeto Buildroot) e execute:

```sh
sh QEMU.sh
```

O script facilita a inicialização do QEMU com o ambiente já configurado. Após a inicialização, acesse o terminal do **Linux emulado**. Os próximos comandos devem ser executados **dentro do QEMU**, não no WSL.

## 2. Carregar o módulo (dentro do QEMU)

Carregue o módulo informando a quantidade máxima de tópicos:

```sh
modprobe pubsub max_topics=2
```

O exemplo permite manter até **2 tópicos** ao mesmo tempo. Se o parâmetro não for informado, o valor padrão definido no código atual é `10`.

Verifique se o módulo e o dispositivo estão disponíveis:

```sh
lsmod
ls -l /dev/pubsub
cat /sys/module/pubsub/parameters/max_topics
```

O último comando deve mostrar `2` no exemplo acima. O parâmetro `max_topics` é definido durante a carga do módulo e não pode ser alterado em tempo de execução nesta implementação (`0444`).

## 3. Exibir as mensagens de log (loglevel)

O módulo registra suas operações com `pr_info()`, mas essas mensagens podem não aparecer automaticamente no terminal devido ao nível de log do console.

Consulte o nível atual:

```sh
cat /proc/sys/kernel/printk
```

Para exibir também as mensagens informativas no console do QEMU, execute:

```sh
dmesg -n 8
```

Alternativamente:

```sh
echo 8 > /proc/sys/kernel/printk
```

Essa alteração é **temporária**: modifica a exibição de mensagens no console durante a execução atual, sem alterar permanentemente a imagem do Buildroot. Também é possível consultar as mensagens armazenadas no buffer do kernel:

```sh
dmesg | tail -30
```

> Nem todas as mensagens do módulo contêm o texto `pubsub`. Por isso, `dmesg | tail -30` pode ser mais útil do que `dmesg | grep pubsub`.

## 4. Arquitetura interna das mensagens

O broker mantém uma **lista global de tópicos**. Cada tópico contém uma **lista de inscritos, identificados por PID**, e cada inscrição mantém sua **própria fila de mensagens**.

```text
topic_list (lista global de tópicos)
│
├── Tópico "teste"
│   │
│   ├── PID 101
│   │   ├── Mensagem 1: "Hello"
│   │   └── Mensagem 2: "World"
│   │
│   └── PID 102
│       ├── Mensagem 1: "Hello"
│       └── Mensagem 2: "World"
│
└── Tópico "alertas"
    │
    └── PID 101
        └── Mensagem 1: "Alerta!"
```

As estruturas utilizadas no módulo são:

- `topic_node`: representa um tópico e sua lista `subscribers`.
- `subscriber_node`: representa um PID inscrito em um tópico e sua fila `messages`.
- `message_node`: representa uma mensagem pendente para um inscrito.

Um mesmo PID pode aparecer em diferentes tópicos, mas suas filas são independentes. Quando alguém publica em `teste`, o módulo enfileira **uma cópia da mensagem para cada inscrito nesse tópico**. Quando um processo lê, só a cópia destinada a ele é retirada da fila.

## 5. Testar o fluxo completo com a aplicação C

A aplicação `pubsub-teste.c`, compilada para a arquitetura do Buildroot e incluída na imagem como `/usr/bin/pubsub-teste`, usa `stdio.h` para se comunicar com `/dev/pubsub`.

Com o módulo carregado, execute **dentro do QEMU**:

```sh
pubsub-teste
```

O teste atual realiza, nessa ordem:

1. `fopen("/dev/pubsub", "r+")`: abre e mantém o dispositivo aberto.
2. `/subscribe teste`: cria o tópico (se necessário) e inscreve o processo.
3. `/publish teste "Hello World!"`: coloca uma cópia da mensagem na fila do inscrito.
4. `/fetch teste`: seleciona o tópico para a próxima leitura.
5. `fread()`: consome uma mensagem da fila e a copia para a aplicação.
6. `fclose()`: encerra a abertura e aciona `release()`, liberando inscrições e mensagens pendentes do processo.

Saída esperada da aplicação:

```text
Received message: Hello World!
```

Para acompanhar os registros do driver:

```sh
dmesg | tail -30
```

Trecho ilustrativo dos logs (o PID varia):

```text
Topic 'teste' created
PID 101 subscribed to 'teste'
Published message to topic 'teste' for 1 subscriber(s)
PID 101 selected topic 'teste'
PID 101 read 12 bytes from 'teste'
PID 101 unsubscribed from 'teste'
Topic 'teste' removed
```

> **Importante:** `/fetch` **seleciona** o tópico; a mensagem só é consumida quando a aplicação chama `fread()`. Não use `cat /dev/pubsub` para validar esse fluxo: o `cat` é outro processo, com outro PID, que não possui a inscrição da aplicação.

## 6. Testes manuais de inscrição e publicação

Os exemplos a seguir servem para desenvolvimento e observação dos logs. Para a entrega, utilize a aplicação em C com `stdio.h`.

Abra `/dev/pubsub` no descritor 3 do shell e **mantenha-o aberto**:

```sh
exec 3<>/dev/pubsub
```

### Inscrever-se em um tópico (`/subscribe`)

```sh
printf '/subscribe teste\n' >&3
```

Se `teste` não existir, o módulo cria o tópico e inscreve o PID que realizou a operação. Repetir a inscrição não cria uma segunda entrada para o mesmo PID.

```sh
dmesg | tail -15
```

Exemplo de log:

```text
Topic 'teste' created
PID 123 subscribed to 'teste'
```

### Publicar mensagens (`/publish`)

```sh
printf '/publish teste "Hello World!"\n' >&3
printf '/publish teste "Mensagem 2"\n' >&3
printf '/publish teste "Mensagem 3"\n' >&3
```

A publicação gera uma cópia de cada mensagem para a fila de cada inscrito. Se o tópico não existir ou não possuir inscritos, a publicação é ignorada.

```sh
dmesg | tail -20
```

Exemplo de log:

```text
Published message to topic 'teste' for 1 subscriber(s)
```

### Selecionar o tópico (`/fetch`)

```sh
printf '/fetch teste\n' >&3
```

Esse comando apenas escolhe qual tópico será lido na próxima chamada de leitura. Para consumir uma mensagem, use `fread()` na aplicação C apresentada na seção 5, mantendo a mesma abertura do dispositivo e a inscrição do processo.

> **Observação:** para esses comandos manuais, use um shell no qual `printf` seja um comando interno (*builtin*), para manter a identidade do processo. Se `printf` for executado como programa externo, o PID da operação poderá ser diferente do PID do shell que abriu o descritor.

## 7. Testar o limite de tópicos

Considerando o módulo carregado com `max_topics=2` e o descritor 3 ainda aberto, após criar o tópico `teste`, crie um segundo:

```sh
printf '/subscribe alertas\n' >&3
```

Agora tente criar um terceiro:

```sh
printf '/subscribe noticias\n' >&3
```

O terceiro tópico não deverá ser criado, pois o limite foi atingido. Consulte:

```sh
dmesg | tail -20
```

Mensagem esperada:

```text
Maximum number of topics reached
```

## 8. Fechar o dispositivo e liberar os recursos

Se utilizou o descritor 3 nos testes manuais, feche-o:

```sh
exec 3>&-
```

A função `release()` remove as inscrições associadas ao processo, libera as mensagens pendentes e remove os tópicos que ficarem vazios.

```sh
dmesg | tail -20
```

Exemplo de logs:

```text
PID 123 unsubscribed from 'teste'
Topic 'teste' removed
PID 123 unsubscribed from 'alertas'
Topic 'alertas' removed
```

No teste com a aplicação C, esse fechamento já acontece automaticamente no `fclose()`.

## 9. Descarregar o módulo

Depois de fechar os programas ou descritores que usam o dispositivo:

```sh
modprobe -r pubsub
```

Verifique a mensagem de remoção:

```sh
dmesg | tail -10
```

## Funcionalidades no estágio atual

| Comando ou interface | Situação | Finalidade |
| --- | --- | --- |
| `/subscribe <topico>` | **Implementado e testado** | Inscreve o PID no tópico, criando-o quando necessário |
| `/publish <topico> "<mensagem>"` | **Implementado e testado** | Enfileira uma cópia por inscrito |
| `/fetch <topico>` | **Implementado e testado** | Seleciona o tópico para leitura |
| `fread()` em `/dev/pubsub` | **Implementado e testado** | Consome uma mensagem pendente por vez |
| Limpeza no `release()` | **Implementado e testado no fluxo básico** | Remove inscrições e libera recursos ao fechar |
| `module_param(max_topics, int, 0444)` | **Implementado e testado** | Limita o número global de tópicos |
| `/unsubscribe <topico>` | **Pendente** | Remove a inscrição sem fechar o dispositivo |
| `/sys/pubsub/<topico>` | **Pendente** | Define o limite de inscritos em cada tópico |
| Teste com múltiplos processos | **Pendente** | Confirma a independência das filas entre PIDs |

## Observações

- Este README pressupõe que `pubsub.ko` e `pubsub-teste` já estejam na imagem; o comando `sh QEMU.sh` apenas inicia o ambiente configurado.
- O dispositivo `/dev/pubsub` é uma interface do kernel, não um arquivo de texto comum.
- A aplicação mantém o dispositivo aberto entre os comandos; isso é necessário para conservar a inscrição e o tópico selecionado em `filep->private_data`.
- As mensagens são armazenadas no espaço de kernel por meio de listas encadeadas; uma leitura bem-sucedida remove apenas a mensagem daquele inscrito.
- O comando `/unsubscribe` ainda **não foi implementado**. A mensagem `unsubscribed` exibida ao executar `fclose()` vem da limpeza automática da função `release()`.
