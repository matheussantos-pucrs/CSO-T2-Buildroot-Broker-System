# Módulo Publish/Subscribe (`pubsub`)

Módulo carregável do kernel Linux (*Loadable Kernel Module* — LKM) que fornece o dispositivo de caracteres `/dev/pubsub`. Ele implementa um mecanismo inicial de comunicação entre processos por tópicos, usando o modelo **Publish/Subscribe**.

> **Pré-requisito:** os comandos deste README devem ser executados **dentro do Linux iniciado no QEMU**, com o módulo `pubsub.ko` já instalado na imagem do Buildroot. Este guia não abrange a instalação do Buildroot, a compilação do módulo nem a inicialização do QEMU.

## 1. Carregar o módulo

Carregue o módulo informando a quantidade máxima de tópicos:

```sh
modprobe pubsub max_topics=2
```

O exemplo permite manter até **2 tópicos** ao mesmo tempo. Se o parâmetro não for informado, o valor padrão no código atual é `10`.

Verifique se o módulo e o dispositivo estão presentes:

```sh
lsmod
ls -l /dev/pubsub
cat /sys/module/pubsub/parameters/max_topics
```

O último comando deve mostrar `2` para o exemplo acima. O parâmetro `max_topics` é definido na carga do módulo e não pode ser alterado em tempo de execução nesta implementação.

## 2. Exibir as mensagens de log

O módulo utiliza `pr_info()` para registrar as operações no log do kernel. Dependendo do nível de log do console, essas mensagens podem não aparecer diretamente no terminal do QEMU.

Consulte a configuração atual:

```sh
cat /proc/sys/kernel/printk
```

Para habilitar a exibição das mensagens informativas no console:

```sh
dmesg -n 8
```

Alternativamente:

```sh
echo 8 > /proc/sys/kernel/printk
```

Essa configuração altera o nível de mensagens mostradas **no console** durante a execução atual; não modifica permanentemente a imagem do Buildroot. Para consultar o buffer de logs sem depender da exibição automática no console:

```sh
dmesg | tail -30
```

> Algumas mensagens do módulo não contêm o texto `pubsub`. Portanto, `dmesg | tail -30` pode ser mais útil do que filtrar apenas por `grep pubsub`.

## 3. Abrir o dispositivo

Para os testes manuais, abra `/dev/pubsub` no descritor de arquivo 3 do shell e **mantenha-o aberto**:

```sh
exec 3<>/dev/pubsub
```

Isso é importante porque o fechamento do dispositivo executa a operação `release`, que remove as inscrições do processo e libera suas mensagens pendentes.

## 4. Inscrever-se em um tópico (`/subscribe`)

```sh
printf '/subscribe teste\n' >&3
```

Se o tópico `teste` não existir, ele será criado. O processo que realizou a inscrição será associado a esse tópico por seu PID. Repetir a inscrição do mesmo processo no mesmo tópico não cria uma inscrição duplicada.

Consulte os registros:

```sh
dmesg | tail -15
```

Exemplo de mensagens esperadas (o PID varia):

```text
Topic 'teste' created
PID 123 subscribed to 'teste'
```

## 5. Publicar mensagens (`/publish`)

Publique uma mensagem no tópico:

```sh
printf '/publish teste "Hello World!"\n' >&3
```

Publique outras mensagens:

```sh
printf '/publish teste "Mensagem 2"\n' >&3
printf '/publish teste "Mensagem 3"\n' >&3
```

O módulo cria uma **cópia de cada mensagem para cada processo inscrito no tópico**, adicionando-a à fila de mensagens pendentes desse processo. Por enquanto, a confirmação é feita pelos logs:

```sh
dmesg | tail -20
```

Exemplo de mensagem esperada:

```text
Published message to topic 'teste' for 1 subscriber(s)
```

Se não houver processos inscritos ou se o tópico não existir, a publicação é ignorada.

## 6. Testar o limite de tópicos

Se o módulo foi carregado com `max_topics=2`, crie mais um tópico:

```sh
printf '/subscribe alertas\n' >&3
```

Agora tente criar um terceiro tópico:

```sh
printf '/subscribe noticias\n' >&3
```

A terceira criação deverá ser recusada, porque o limite de dois tópicos já foi atingido. Consulte:

```sh
dmesg | tail -20
```

Mensagem esperada:

```text
Maximum number of topics reached
```

## 7. Fechar o dispositivo e liberar recursos

Ao terminar os testes, feche o descritor:

```sh
exec 3>&-
```

A função `release` percorre os tópicos e remove as inscrições associadas ao processo, libera suas mensagens pendentes e remove tópicos que ficarem sem inscritos.

Verifique os registros:

```sh
dmesg | tail -20
```

Exemplo de mensagens esperadas:

```text
PID 123 unsubscribed from 'teste'
Topic 'teste' removed
PID 123 unsubscribed from 'alertas'
Topic 'alertas' removed
```

## 8. Descarregar o módulo

Depois de fechar os arquivos que usam o dispositivo:

```sh
modprobe -r pubsub
```

Verifique a mensagem de remoção:

```sh
dmesg | tail -10
```

## Comandos disponíveis no estágio atual

| Comando | Situação | Finalidade |
| --- | --- | --- |
| `/subscribe <topico>` | Implementado e testado | Inscreve o PID no tópico, criando-o quando necessário |
| `/publish <topico> "<mensagem>"` | Implementado e testado por logs | Enfileira uma cópia da mensagem por inscrito |
| `/fetch <topico>` | **Pendente** | Selecionar o tópico para a próxima leitura |
| Leitura via `fread()` | **Pendente** | Consumir uma mensagem pendente por vez |
| `/unsubscribe <topico>` | **Pendente** | Remover inscrição e mensagens do tópico |
| `/sys/pubsub/<topico>` | **Pendente** | Configurar o limite de inscritos por tópico |

## Observações

- Os exemplos com `exec` e `printf` são **testes manuais de desenvolvimento**; a entrega do trabalho exige uma aplicação em espaço de usuário utilizando funções da `stdio.h`, mantendo o dispositivo aberto entre os comandos.
- Para validar comunicação entre múltiplos processos, serão necessários testes com instâncias distintas dessa aplicação, cada uma com sua própria inscrição.
- O limite global de tópicos é configurado com `module_param(max_topics, int, 0444)`.
- As mensagens pendentes são mantidas nas listas do kernel até serem consumidas futuramente pela rotina de leitura ou liberadas com o fechamento do dispositivo.
