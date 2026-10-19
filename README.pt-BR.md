# MoobFS

**A memória de um agente como arquivos comuns que o kernel consegue consultar, com as permissões de arquivo decidindo quem vê o quê.**

[Site do projeto](https://studiomoob.github.io/llmoobfs/) · [Medições](docs/history/0088-m4-report.md) · [Experimente numa VM](#experimente-numa-vm) · [Como ajudar](docs/help-wanted.md) · [Índice da documentação](docs/README.md) · [FAQ](docs/faq.md) · [Roadmap](ROADMAP.md) · [English](README.md)

[![CI](https://github.com/studiomoob/llmoobfs/actions/workflows/ci.yml/badge.svg)](https://github.com/studiomoob/llmoobfs/actions/workflows/ci.yml)
[![License: GPL-2.0-only](https://img.shields.io/badge/license-GPL--2.0--only-blue)](LICENSE)
[![OpenSSF Scorecard](https://api.scorecard.dev/projects/github.com/studiomoob/llmoobfs/badge)](https://scorecard.dev/viewer/?uri=github.com/studiomoob/llmoobfs)
[![OpenSSF Best Practices](https://www.bestpractices.dev/projects/15408/badge)](https://www.bestpractices.dev/projects/15408)

> Este arquivo é um espelho de cortesia do [README em inglês](README.md), que prevalece em caso
> de diferença ([ADR-0010](docs/decisions/0010-english-everywhere.md)).

> **Experimental. Não é para produção.** Uma série de kernel (6.12), testada só em VM; ainda sem
> revisão humana externa (duas revisões independentes do código, feitas por assistentes de IA,
> diários 0111 e 0114). A última tag, `v0.3.0-experimental`, é anterior a correções de segurança:
> compile a partir da `main`. Detalhes em [Estado](#estado).

## O problema

Um agente de LLM que lembra entre sessões costuma guardar as memórias como arquivos e, ao lado
deles, um banco ou índice que registra o que cada uma é: o tipo, o projeto, a data. A aplicação
precisa manter os dois em sincronia a cada escrita. E qual usuário ou agente pode ver qual memória
é decidido no código da aplicação, não pelo sistema operacional, que já guarda as permissões dos
arquivos.

## O que é o MoobFS

O MoobFS é um módulo experimental do kernel Linux (GPL-2.0, sem FUSE, sem daemon) que se empilha
sobre um diretório de ext4, btrfs ou XFS e leva essas duas tarefas para o sistema de arquivos. Ele
tem três partes:

1. **Atributos tipados em arquivos comuns.** Uma memória é um arquivo; seus campos são atributos
   estendidos `user.moob.*`, cada um com um tipo que o kernel verifica na escrita. Sem o módulo, o
   diretório continua sendo uma árvore comum que `grep`, `getfattr`, `git` e `rsync` leem.
2. **Um índice no kernel.** O módulo indexa esses atributos em RAM e atualiza o índice quando um
   atributo é escrito. Nada é varrido na montagem: a primeira consulta a um diretório constrói a
   parte do índice daquele diretório, que é liberada de novo quando falta memória.
3. **Uma consulta limitada por diretório e autorizada pelas permissões de arquivo.** Um `ioctl`
   (`QUERY2`) num diretório responde a até quatro comparações unidas por E, opcionalmente
   ordenadas por um atributo. Ele devolve só os arquivos que todos os que podem entrar naquele
   diretório conseguem ler.

Ele guarda e encontra memórias. Escolher o que é relevante, embeddings, ranqueamento e montagem do
prompt ficam na aplicação.

## Quatro números

Os quatro são de nível piloto e de uma só máquina (dedicada, com SSD SATA, Debian 13, kernel
6.12.111, ext4 abaixo da camada): uma semente de dados e uma rodada por escala na comparação com o
cliente. Os três primeiros comparam o `QUERY2` com o banco SQLite de um cliente de memória real,
nas seis consultas estruturadas do próprio cliente, emitidas por uma ferramenta de teste dos dois
lados.

| | O quê | Condição | Fonte |
| --- | --- | --- | --- |
| **0 diferenças** | Respostas do `QUERY2` contra o SQLite do cliente | Todas as verificações, de 150 a 60 000 memórias | [relatório, pergunta 4](docs/history/0088-m4-report.md); diários [0064](docs/history/0064-client-stage2-first-run.md), [0068](docs/history/0068-client-stage2-scale.md) |
| **5 a 6,5 vezes mais rápido** | A camada vence num formato de consulta (um conjunto de tipos); cache quente, mediana | 2 000 a 60 000 memórias. Parte da diferença vem do esquema deste cliente, que guarda o campo numa coluna JSON | [relatório, pergunta 4](docs/history/0088-m4-report.md); [diário 0068](docs/history/0068-client-stage2-scale.md) |
| **0,71 a 0,51 da velocidade** | A camada perde no percurso ordenado longo; cache quente, mediana | Mais lenta em todas as escalas, e mais quanto maior a resposta: cerca de metade da velocidade do SQLite com 60 000 memórias | [relatório, pergunta 4](docs/history/0088-m4-report.md); [diário 0068](docs/history/0068-client-stage2-scale.md) |
| **2,7 s** | A primeira consulta a um domínio custa uma varredura dos arquivos dele; a montagem em si leva 0,3 s | Um domínio de 7 459 arquivos entre 60 000, cache frio, no SSD (0,1 s com cache quente). Antes do diário 0098 a montagem varria todos os arquivos: 19 s com cache frio. O arquivo SQLite do cliente abriu frio em 17 a 23 ms | [diário 0098](docs/history/0098-index-on-demand.md); [relatório, pergunta 2](docs/history/0088-m4-report.md) |

Dos outros quatro formatos, a camada é mais rápida em dois e empata em dois. A comparação conta só
os caminhos devolvidos, não a leitura dos campos de cada resultado, que o SQL devolve na mesma
chamada. Mais em [O que foi medido](#o-que-foi-medido).

## Veja funcionando

Cada diretório de primeiro nível da montagem é um domínio de permissão. O root marca quatro
arquivos de memória; depois a Alice (uid 1001) consulta. Ela vê a própria memória, não consegue
entrar no domínio do Bob e não pode rodar a consulta da montagem inteira, que é para
administradores:

```text
# (prepared on the lower ext4: alice/ uid 1001 0700, bob/ uid 1002 0700, team/ 0755)
# mount -t moobfs_layer -o lower=/lower none /mnt/layer
# moobfs-attr set /mnt/layer/alice/cache.md kind string decision
# moobfs-attr set /mnt/layer/bob/deploy.md kind string decision
# moobfs-attr set /mnt/layer/team/style.md kind string decision
# moobfs-attr set /mnt/layer/team/salary.md kind string decision
# moobfs-attr list /lower/alice/cache.md
kind	string	decision
# stat -c '%A %u %n' /mnt/layer/team/*
-rw------- 1002 /mnt/layer/team/salary.md
-rw-r--r-- 0 /mnt/layer/team/style.md
# query --uid 1001 /mnt/layer/alice q2 kind eq string decision
16	cache.md
# query --uid 1001 /mnt/layer/team q2 kind eq string decision
18	style.md
# query --uid 1001 /mnt/layer/bob q2 kind eq string decision
open: Permission denied
# query --uid 1001 /mnt/layer kind eq string decision
MOOB_IOC_QUERY: Operation not permitted
# query /mnt/layer kind eq string decision
16	alice/cache.md
17	bob/deploy.md
18	team/style.md
19	team/salary.md
```

No domínio compartilhado `team/` ela recebe só o `style.md`. O `salary.md` só o Bob lê, e um
domínio indexa apenas arquivos que todos os que podem entrar nele conseguem ler; por isso ele fica
fora do índice de `team/` para qualquer chamador, inclusive o Bob e o root. Arquivos privados ficam
num domínio próprio ([spec-query-v2](docs/spec-query-v2.md) §3).

Esta é a saída real da etapa de demonstração da suíte de VM do projeto (Debian 6.12.111 no QEMU,
ext4 por baixo): as linhas `DEMO| ` de `build/vm/console.log` sem o prefixo, e sem as duas
chamadas `GET_INFO` do final. Linhas que começam com `#` são comandos executados como root. `query`
é o cliente de teste em [`tests/query.c`](tests/query.c), já que ainda não há CLI; `--uid 1001`
troca para esse usuário antes da chamada, e a forma `q2` é o `MOOB_IOC_QUERY2` num diretório de
domínio. O script é o bloco de demonstração de [`vm/suite.sh`](vm/suite.sh). Para obter essa saída
na sua própria execução, veja [Experimente numa VM](#experimente-numa-vm).

## Estado

**Experimental. Só tags de pré-lançamento do código-fonte; a última, `v0.3.0-experimental`, é anterior
às correções dos achados F6 a F12, então compile a partir da `main`. Não é para produção.** Fatos em
2026-10-18:

- **Uma série de kernel**, em VM: Debian 6.12.111 no QEMU e o 6.12.112 upstream sob
  KASAN/UBSAN/lockdep, obrigatório em todo pull request que mexe no módulo; x86-64; sistemas de
  arquivos de baixo ext4, btrfs e XFS apenas. Os testes da codificação de atributos também passam em
  s390x big-endian, com emulação; execuções em arm64 estão sendo medidas ([diário 0124](docs/history/0124-big-endian-pass-arm64-without-kvm.md)).
- **Testado com** injeção de falhas dentro do módulo, o grupo `quick` do fstests nos três sistemas
  de arquivos de baixo, syzkaller (três rodadas de seis horas; um defeito achado e corrigido), uma
  primeira rodada do KCSAN (nenhuma corrida no módulo) e SELinux em modo enforcing (diários 0091,
  0096, 0104, 0109, 0112, 0115, 0133).
- **Ainda sem revisão humana externa.** Foi pedida em 2026-10-13. Duas revisões independentes do
  código, feitas por assistentes de IA ([diário 0111](docs/history/0111-review-5582e06.md),
  [diário 0114](docs/history/0114-second-inspection-of-5582e06.md)) e conferidas contra o kernel,
  acharam quatro defeitos de segurança (achados F9 a F12 do ADR-0011), relatados como avisos de
  segurança privados e corrigidos nos diários 0116 e 0117; nossos próprios testes e revisões
  acharam outros antes. As tags até a `v0.3.0-experimental` carregam defeitos corrigidos depois
  ([changelog](CHANGELOG.md)).
- **As medições são de nível piloto**, de uma máquina; o relatório que as reúne é o
  [diário 0088](docs/history/0088-m4-report.md). Não são uma afirmação geral de velocidade.
- O índice vive **só em RAM** e não pode ir para o swap. Ele é construído por domínio na primeira
  consulta a cada um e liberado de novo sob pressão de memória ou para caber no orçamento, o
  consultado há mais tempo primeiro; uma consulta que precisa de todos os domínios (o `QUERY` da
  montagem inteira) custa o que a varredura da montagem custava.
- Montar exige **`CAP_SYS_ADMIN` no namespace de usuário inicial**.
- **A interface pode mudar**, sem promessa de compatibilidade.

Para qualquer coisa além de uma VM descartável, SQLite ou arquivos comuns continuam sendo a
recomendação ([when-not-to-use.md](docs/when-not-to-use.md)). A lista completa está em
[Limitações](#limitações).

## Como ajudar

O projeto tem um mantenedor e ainda nenhuma revisão humana externa. O que mais ajudaria, organizado por
quanto conhecimento de kernel exige, está em **[docs/help-wanted.md](docs/help-wanted.md)**: uma
revisão do modelo de credenciais ou das travas do índice, se você conhece o VFS; uma verificação
de se o seu sistema de memória se encaixa, se você constrói um; uma rodada dos testes na sua
máquina ou em outro kernel; e algumas issues que não exigem conhecimento de kernel. As regras para
mudanças estão em [CONTRIBUTING.md](CONTRIBUTING.md).

---

## Como funciona

```text
client (git, sqlite3, a script, an agent)
   │ write tmp, setxattr user.moob.*, fsync, rename        │ QUERY2 ioctl on a domain directory
   ▼                                                        ▼
Linux VFS ──► moobfs_layer.ko: validates typed attributes, keeps an in-RAM index
              partitioned by domain, checks the caller's permission on the directory,
              delegates I/O through fs/backing-file.c
   ▼
lower filesystem (ext4, btrfs or XFS): real files, real xattrs, journal, reflink
```

Escrever uma memória é uma escrita POSIX comum mais `setxattr`; a camada valida a codificação
(byte de tipo mais conteúdo) antes que o xattr chegue ao sistema de arquivos de baixo e atualiza
o índice. Encontrar memórias é uma chamada `QUERY2` num diretório de domínio: até quatro
predicados de atributo unidos por E, respondidos pela partição daquele domínio no índice depois de
uma verificação de permissão no diretório. Um arquivo que nem todos que entram no domínio podem
ler fica fora da partição ([spec-query-v2](docs/spec-query-v2.md) §3). O cliente então abre o
arquivo normalmente. Para listas IN, OR, ordenação e contagens há uma extensão opcional do SQLite
([sqlite/](sqlite/README.md), protótipo): SQL sobre um domínio sem banco de dados, e cada linha
continua sendo uma resposta do `QUERY2`. Desmonte a camada e o diretório de baixo é uma árvore comum que `grep`,
`getfattr`, `git` e `rsync` leem sem o módulo. Detalhes com citações do código:
[visão de arquitetura](docs/architecture-overview.md); o mesmo fluxo como diagrama:
[site/plan.html](https://studiomoob.github.io/llmoobfs/plan.html#flow).

**O que é.** Um módulo nativo do kernel Linux, `moobfs_layer.ko`, montado como camada empilhada
sobre um diretório de um sistema de arquivos maduro (ext4, btrfs ou XFS). Memórias são arquivos
comuns; seus metadados são atributos estendidos tipados (`user.moob.*`) que o módulo valida na
escrita e indexa em RAM, particionados por domínio de permissão (por padrão, cada diretório de
primeiro nível). Um ioctl `QUERY2` num diretório de domínio responde a predicados de atributo em
conjunção a partir do kernel, devolvendo só arquivos que todos que podem entrar no domínio
conseguem ler; o `QUERY` antigo, da montagem inteira, verifica cada resultado contra as permissões
POSIX e as ACLs e, com domínios ligados (o padrão desde a fase Q2a), é só para administradores.
Tudo continua legível com ferramentas Unix comuns sem a camada. Um formato de imagem somente
leitura do M1, com conteúdo físico compartilhado, é mantido como regressão.

**O que não é.** Não é FUSE nem daemon: não há servidor em espaço de usuário, socket nem processo
em segundo plano. Não há busca semântica ou vetorial no kernel: ranqueamento, embeddings e
montagem de contexto são do cliente. Não substitui um banco de dados; hoje, para qualquer coisa
além de uma VM descartável, SQLite ou arquivos comuns continuam sendo a recomendação
([value assessment](docs/value-assessment.md)). Não há persistência do índice, nem snapshots, nem
transações próprias; durabilidade, reflink e dedupe são do sistema de arquivos de baixo.

**O objetivo.** O MoobFS quer oferecer uma base de armazenamento, no nível do sistema de arquivos,
para a memória e o contexto persistentes consumidos por agentes baseados em LLM: reter memórias
como observações, decisões e registros de tarefas entre sessões, compartilhar conteúdo repetido e,
no futuro, preservar revisões que os agentes possam recuperar como contexto. O sistema de arquivos
guarda e expõe a memória como arquivos. A aplicação do agente é responsável por selecionar o
material relevante e montar o contexto do LLM. A recuperação semântica e a montagem de contexto
ainda precisam ser desenhadas e integradas; o MoobFS hoje não as oferece nem amplia a janela de
contexto de um modelo.

**O que um sistema de memória precisa para usá-lo.** Um arquivo por memória, campos como
atributos tipados, e cada escopo que ele isola como um diretório numa profundidade fixa:
[client-contract.md](docs/client-contract.md). Os riscos que ele assume ao fazer isso, e o que
responde a cada um: [adoption-risks.md](docs/adoption-risks.md).

**Como conferir qualquer afirmação daqui.** Rode `make vm-test` e leia `build/vm/console.log`
([guia de testes](docs/testing.md)); prepare o ambiente com
[docs/development.md](docs/development.md).

## O que foi medido

O relatório que responde às quatro perguntas do [plano de benchmark](docs/benchmark-plan.md) é o
[docs/history/0088-m4-report.md](docs/history/0088-m4-report.md). Toda campanha é de nível piloto:
uma semente de dados, no máximo três repetições, muitas vezes uma só rodada, e um ruído entre
rodadas de cerca de 20 a 25 % em valores de dezenas de microssegundos. Números de máquinas
diferentes não são comparados entre si. O código do índice mudou depois da maior parte dessas
medições (diários [0082](docs/history/0082-index-lock-and-reclaim.md),
[0083](docs/history/0083-index-inspection-fixes.md)); só as duas campanhas abaixo rodaram depois.

O que a camada acrescenta por operação, no módulo atual (10 000 memórias, medianas, sistema de
baixo puro contra a camada): uma leitura por caminho vai de 8,5 para 11,0 µs no ext4 (10,1 para
12,0 no btrfs, 9,8 para 15,0 no XFS); gravar um atributo, de 8,9 para 21,7 µs (8,9 para 21,3; 6,0
para 11,6); criar uma memória de forma durável não muda dentro do ruído, porque os flushes do
dispositivo levam 12 a 14 ms.

O que um banco ao lado custa na escrita, que este projeto supunha e nunca tinha medido: quase
nada. Publicar uma memória de forma durável leva 6,6 a 8,5 ms só com o arquivo, e 6,6 a 8,7 ms com
uma linha acrescentada num banco SQLite no modo habitual; pela camada, com oito atributos, 7,0 a
9,8 ms. Portanto a camada não deixa as escritas mais baratas. O que ela remove é a segunda cópia
dos campos, que pode discordar dos arquivos depois de uma queda.

Onde a camada vence:

- **Consultas seletivas dentro de um domínio.** Num conjunto sintético de 60 000 memórias, o
  `QUERY2` foi de 1,1 a 6 vezes mais rápido que o SQLite com índices compostos nos cinco formatos
  medidos, sendo o limite inferior um empate no melhor caso do SQLite
  (diários [0044](docs/history/0044-q2-measured-equality-plan.md),
  [0047](docs/history/0047-sqlite-baseline-macmini.md),
  [0048](docs/history/0048-recent-optimization.md), [0049](docs/history/0049-q2-btrfs-xfs.md)).
- **Três dos seis formatos de consulta de um cliente real** são mais rápidos, por 5 a 6,5 vezes e
  1,6 a 3,8 vezes nos dois mais claros, com respostas iguais em todas as verificações de 150 a
  60 000 memórias ([diário 0068](docs/history/0068-client-stage2-scale.md)).
- **Esse cliente roda sem mudanças na camada**: seus 1 147 testes passam e os resultados de
  recuperação são idênticos, depois que um defeito da camada exposto por ele foi corrigido
  (diários [0054](docs/history/0054-real-client-plan.md),
  [0057](docs/history/0057-rename-over-deleted-name.md)).

Onde perde, empata ou custa:

- **Percurso ordenado longo**: 0,71 a 0,51 da velocidade do SQLite, pior quanto maior a resposta.
  Dois outros formatos do mesmo cliente empatam.
- **A primeira interface** (`QUERY` v1, na raiz da montagem) é mais lenta que o SQLite em todo
  formato que devolve resultados: 18 vezes mais lenta com 60 000 memórias numa campanha
  ([diário 0029](docs/history/0029-query-scale-series.md)).
- **Campos dos resultados.** Uma consulta devolve números de inode e caminhos. Ler todos os
  atributos de um resultado custa cerca de 16 µs por arquivo, contra cerca de 0,9 µs por
  resultado para a consulta; o SQL devolve as colunas na mesma chamada, e nenhuma comparação acima
  conta isso ([diário 0072](docs/history/0072-m3-phase-3c.md)).
- **Tempo de montagem e primeira consulta.** Com o índice construído sob demanda, 0,31 s com cache
  frio e 0,004 s com cache quente para montar 60 000 arquivos no SSD SATA; a primeira `QUERY2`
  num domínio de 7 459 deles, 2,7 s frio e 0,10 s quente; o `QUERY` da montagem inteira logo após
  montar, 19,7 s frio e 0,74 s quente ([diário 0098](docs/history/0098-index-on-demand.md)). Antes,
  a montagem varria tudo: 0,7 s quente e 18,5 a 19,3 s frio; 126 s frio num disco de nuvem
  (diários [0031](docs/history/0031-remount-time.md), [0055](docs/history/0055-measurement-gaps.md)).
- **Memória.** Cerca de 0,5 a 0,6 KiB de memória do kernel por arquivo, que não vai para o swap,
  pela contagem do próprio índice, que subconta. O orçamento padrão de 128 MiB nunca foi atingido
  num teste.
- **As escritas do cliente real** ficaram cerca de 10 % mais lentas numa rodada, e uma página que
  ele escreve fica consultável cerca de 11 ms depois, não quando a escrita retorna: um processo à
  parte copia os campos para atributos ([diário 0071](docs/history/0071-client-stage2-live-mirror.md)).
  O próprio cliente nunca chamou o `QUERY2`; uma ferramenta de teste emitiu as consultas.
- **Reflink e dedupe** funcionam pela camada, mas a economia é do sistema de arquivos de baixo e é
  pequena para arquivos pequenos: 6,7 % no btrfs e 13,6 % no XFS num conjunto em que 15 % dos
  arquivos são cópias, nenhuma no ext4 ([diário 0085](docs/history/0085-m4-question-3-space.md)).

Não medido: CPU e memória por consulta, concorrência, a latência de um segundo cliente,
repetições com intervalos de confiança. Resultados brutos: [bench/results/](bench/results/README.md).

## Experimente numa VM

Início rápido. Com o Docker rodando, a partir da raiz do repositório. A execução completa leva de 7
a 8 minutos num runner x86-64 do GitHub (o job "Native Linux VM" do CI roda exatamente este
comando); no Apple Silicon a VM roda com emulação de CPU e demora mais:

```sh
git clone https://github.com/studiomoob/llmoobfs.git
cd llmoobfs
make vm-test                          # builds the tools and modules, boots a disposable QEMU VM
grep '^DEMO| ' build/vm/console.log   # the demonstration above, from your own run
```

Só para a demonstração, que é mais curta:

```sh
MOOB_ONLY=demo make vm-test           # ends in MOOBFS_ONLY_PASS, which is not a complete run
grep '^DEMO| ' build/vm/console.log
```

A execução completa compila as ferramentas Linux e os dois módulos e depois sobe uma VM QEMU
x86-64 dedicada com o kernel Debian **6.12.111+deb13-amd64**. Não precisa de contêiner
privilegiado nem de carregar módulos no host; nunca carregue os módulos no seu host. No Apple
Silicon usa emulação de CPU; a primeira compilação baixa pacotes do kernel e demora mais. A
preparação do ambiente está em [docs/development.md](docs/development.md); o que cada marcador
prova e não prova está em [docs/testing.md](docs/testing.md). Uma execução bem-sucedida termina
com:

```text
SHARED: inode=2/3 physical=12288 length=16384
MOUNTED_READ_TESTS_PASS
WRITABLE_MOUNT_REJECTED
MOOBFS_VM_PASS: shared extents, independent inodes, reads, readonly, remount, 15 malformed images
LAYER_LOWER_PASS: ext4 reflink=no
LAYER_LOWER_PASS: btrfs reflink=yes
LAYER_LOWER_PASS: xfs reflink=yes
DEMO_PASS: QUERY2 per domain, v1 refused to a user, heterogeneous file left out
MOOBFS_ACCEPTANCE_PASS: git, sqlite wal, memory client, dm-log-writes replay, dm-flakey
MOOBFS_LAYER_PASS: pass-through over ext4, btrfs and xfs; layer tests; typed attributes and tools; index and QUERY equal to the oracle; fsx 9000 ops each; reflink on btrfs/xfs; nolinks; unload
```

### As ferramentas de imagem do M1 (regressão)

O marco M1, somente leitura, validou que arquivos separados podem referenciar os mesmos bytes
imutáveis: uma ferramenta offline cria a imagem e um módulo nativo serve as leituras pelo VFS. Ele
demonstra compartilhamento físico, não um fluxo de memória de agente. As ferramentas precisam de
um compilador C11, Make e Python 3 no Linux ou no macOS:

```sh
make test
printf 'A memory shared by two files.\n' > build/memory.txt
build/host/mkfs.moobfs build/demo.img alpha.txt=build/memory.txt beta.txt=build/memory.txt
build/host/fsck.moobfs build/demo.img
```

O relatório JSON mostra dois números de inode referenciando o mesmo objeto e o mesmo deslocamento
físico. O criador se recusa a sobrescrever uma imagem existente; o `fsck.moobfs` verifica imagens
e não as repara. O fixture da VM contém 12.562 bytes lógicos referenciados por cada arquivo,
gravados uma vez; o FIEMAP mostra a mesma alocação de 16.384 bytes e a marca de compartilhado
para os dois inodes. Metadados e alinhamento ainda contam no tamanho total da imagem.

| Componente | Comportamento no M1 |
| --- | --- |
| Módulo nativo | Montagem pelo VFS, diretório plano, leituras com buffer via iomap, FIEMAP |
| Criador de imagem | Criação offline determinística, deduplicação de bytes exatos, sem sobrescrita |
| Verificador | Verificações estruturais, limites, CRC32C dos metadados, SHA-256 do conteúdo |
| Limites | 128 arquivos/objetos, 64 KiB por objeto, nomes ASCII imprimíveis de até 63 bytes |
| Validação | 9 testes das ferramentas, sanitizers nas ferramentas, montagens reais no QEMU e 15 imagens malformadas |

As imagens são imutáveis enquanto montadas. Os arquivos têm inodes independentes e contagem de
links 1; isso não é hard link. O módulo valida todo o conteúdo na montagem, o que acrescenta uma
varredura completa. Compartilhar disco não garante compartilhar page cache. O módulo M1 não rodou
sob KASAN (o arnês KASAN roda a suíte da camada), e não existe benchmark dele.

## Limitações

- Uma série de kernel testada (Debian 6.12.111, 6.12.112 upstream sob KASAN), x86-64, em VM; as
  APIs do kernel dependem da versão, e o alvo de teste fixado não é promessa de compatibilidade. O
  módulo não rodou em outra arquitetura; só os testes da codificação, em espaço de usuário, rodaram
  em big-endian.
- O índice é só em RAM, não vai para o swap e é limitado por `index_budget` (padrão 128 MiB). Cada
  domínio é construído por uma varredura linear na primeira consulta depois da montagem, e de novo
  depois de ser liberado ou de ter um diretório renomeado dentro dele; o orçamento e o shrinker só
  foram testados em árvores pequenas (diário 0101).
- Montar exige `CAP_SYS_ADMIN` no namespace de usuário inicial; montagens em namespace de usuário
  e idmapped são recusadas, assim como uma montagem idmapped como `lower=`; não há contêineres sem
  root.
- As UAPIs v1 e v2 podem mudar, sem promessa de compatibilidade ([compatibility.md](docs/compatibility.md)); as tags `-experimental` são instantâneos do código-fonte, sem binários, empacotamento nem suporte.
- Só existem números de nível piloto: duas máquinas, uma semente de dados, no máximo três
  repetições (diários 0025 a 0085). Nenhuma afirmação de velocidade ou eficiência além das
  condições descritas em [O que foi medido](#o-que-foi-medido) pode ser feita a partir deles; não
  existe comparação com FUSE, e a comparação com o sistema de arquivos de baixo puro no módulo atual
  está pendente.
- Sem revisão humana externa. Duas revisões independentes do código, feitas por assistentes de IA
  (diários [0111](docs/history/0111-review-5582e06.md) e [0114](docs/history/0114-second-inspection-of-5582e06.md)),
  e os testes do próprio projeto acharam e corrigiram defeitos do lado do kernel, alguns de
  segurança; os achados estão no [ADR-0011](docs/decisions/0011-threat-and-credential-model.md)
  (F1 a F12), e os defeitos anteriores nos diários [0015](docs/history/0015-phase-2e-investigation.md),
  [0016](docs/history/0016-kasan-lockdep-harness.md),
  [0035](docs/history/0035-phase-q2a-domain-query.md),
  [0082](docs/history/0082-index-lock-and-reclaim.md) e
  [0083](docs/history/0083-index-inspection-fixes.md). Todas as tags até agora carregam defeitos
  corrigidos depois: compile a partir da `main`. O fstests rodou só com o grupo `quick`
  ([vm/fstests/](vm/fstests/README.md)); o syzkaller rodou três rodadas de seis horas; a última
  medição ficou abaixo do limiar de cobertura do item S3 do [roteiro de segurança](docs/security-roadmap.md). O SELinux em
  modo enforcing foi exercitado uma vez (diário 0104); AppArmor e Smack não foram.
- A autorização do `QUERY` vale só no momento da consulta (contrato TOCTOU); falsos negativos
  possíveis depois de renomear um diretório, até remontar ([spec-m3](docs/spec-m3.md) §3.1).
- A atomicidade entre arquivo e metadados é uma convenção do cliente; durabilidade, reflink e
  dedupe são do sistema de arquivos de baixo. A aceitação rodou só sobre ext4.
- Os números de inode são os do sistema de baixo, e um arquivo é identificado por esse número. Um
  subvolume ou snapshot btrfs **dentro** da árvore de baixo numera os arquivos do zero de novo,
  então a camada não entra nele (`EXDEV`) e o deixa fora do índice. Mantenha os snapshots fora do
  diretório que a camada cobre.
- Os dados são escritos com as credenciais de quem montou, sem `CAP_SYS_RESOURCE`, então os
  limites de cota valem. No ext4 com o dono padrão da reserva (uid 0), usuários comuns escrevendo
  pela camada ainda podem usar os blocos reservados ao root; monte o sistema de baixo com outro
  `resuid`, ou zere a reserva, se isso importar.
- `noatime`, `relatime` e `strictatime` na montagem da camada não têm efeito; os tempos de acesso
  seguem as opções de montagem do sistema de arquivos de baixo.

## O que está implementado, com evidência

"Testado em" indica o kernel e os sistemas de arquivos de baixo que realmente rodaram; nada aqui
significa prontidão para produção. Desde o [ADR-0007](docs/decisions/0007-stacked-memory-layer.md)
o MoobFS é construído como camada empilhada nativa sobre um sistema de arquivos maduro; o
experimento M1 está congelado e roda como regressão na mesma VM.

| Item | Diário | Testado em |
| --- | --- | --- |
| Imagem somente leitura do M1: criador offline, verificador, módulo nativo, dois inodes compartilhando um conteúdo (FIEMAP), 15 imagens malformadas rejeitadas | [0002](docs/history/0002-native-read-milestone.md), [formato](docs/format-m1.md) | Debian 6.12.111 (QEMU), dispositivo loop |
| Fases 2a–2b: pass-through empilhado (`mount -t moobfs_layer -o lower=<dir>[,nolinks]`): arquivos, diretórios, rename, links, xattr, mmap, fsync; reflink/dedupe/`copy_file_range` delegados, FIEMAP; `fsx` | [0011](docs/history/0011-phase-2a-stacked-passthrough.md), [0012](docs/history/0012-phase-2b-full-passthrough.md) | Debian 6.12.111; ext4, btrfs, XFS |
| Fase 2c: atributos tipados `user.moob.*` validados no kernel; `moobfs-attr`, `moobfs-check`; testes unitários no host; alvo libFuzzer | [0013](docs/history/0013-phase-2c-typed-attributes.md) | Testes no host (Linux/macOS), VM sobre os três sistemas |
| Fase 2d: índice em memória, `QUERY` e `GET_INFO` (UAPI v1), respostas idênticas ao oráculo `moobfs-index`, também depois de rename, unlink, removexattr e remontagem; `noindex`, `index_budget` | [0014](docs/history/0014-phase-2d-index-query.md) | Debian 6.12.111; ext4, btrfs, XFS |
| Fase 2e: aceitação com git, SQLite em modo WAL concorrente, cliente de memória próprio; replay de queda de energia com `dm-log-writes`; erros de I/O com `dm-flakey`; dois bugs da camada achados e corrigidos | [0015](docs/history/0015-phase-2e-investigation.md) | Debian 6.12.111; só ext4 |
| Arnês KASAN/UBSAN/lockdep rodando a mesma suíte; um terceiro defeito achado e corrigido | [0016](docs/history/0016-kasan-lockdep-harness.md) | 6.12.112 upstream, virtme-ng |
| M3 fase 3b: autorização POSIX por resultado no `QUERY`; dois uids, dois namespaces, chmod e ACLs, remontagem, montagens idmapped e em namespace de usuário recusadas, corrida de 5 segundos de chmod/rename/unlink contra o `QUERY` | [0020](docs/history/0020-phase-3b-query-authorization.md), [spec-m3](docs/spec-m3.md) §3 | Debian 6.12.111 e KASAN 6.12.112; ext4, btrfs, XFS |
| Fase Q2a: `QUERY2` limitado por domínio (UAPI v2) num diretório de domínio, índice particionado por domínio e uma regra de homogeneidade, v1 restrita a administradores enquanto `domain_depth` estiver ligado, `EXDEV` para diretórios entre domínios; igual a `moobfs-index --domain` (medido: diários [0043](docs/history/0043-timing-channel-query2.md), [0044](docs/history/0044-q2-measured-equality-plan.md), [0047](docs/history/0047-sqlite-baseline-macmini.md)) | [0035](docs/history/0035-phase-q2a-domain-query.md), [spec-query-v2](docs/spec-query-v2.md) | Debian 6.12.111; ext4, btrfs, XFS |
| Fase Q2b: `QUERY2` ordenado por um atributo, crescente ou decrescente, e paginado por um cursor autenticado; igual a `moobfs-index --domain --order`; "N mais recentes" medido e otimizado ([0048](docs/history/0048-recent-optimization.md)) | [0036](docs/history/0036-phase-q2b-order-cursor.md) | Debian 6.12.111; ext4, btrfs, XFS |
| Fase Q2c: interseção leapfrog de termos de igualdade no `QUERY2`; igual ao oráculo, plano verificado pela ordem dos resultados, medido no [0044](docs/history/0044-q2-measured-equality-plan.md) | [0037](docs/history/0037-phase-q2c-leapfrog.md) | Debian 6.12.111; ext4, btrfs, XFS |
| Fase Q2d: termos "ausente ou ..." e lotes de até 8 requisições `QUERY2` por chamada; igual ao oráculo | [0038](docs/history/0038-phase-q2d-missing-batch.md) | Debian 6.12.111; ext4, btrfs, XFS |
| Testes sob o módulo: grupo `quick` do fstests com lista de exclusões, injeção sistemática de falhas de alocação dentro do módulo | [0091](docs/history/0091-s2-fstests-clean-with-exclusions.md), [0096](docs/history/0096-fault-injection.md) | Debian 6.12.111; KASAN 6.12.112; ext4, btrfs, XFS |
| Índice construído por domínio na primeira consulta, liberado sob pressão de memória ou acima do orçamento (shrinker); tracepoints | [0098](docs/history/0098-index-on-demand.md), [0101](docs/history/0101-domains-released.md), [0102](docs/history/0102-index-tracepoints.md), [ADR-0012](docs/decisions/0012-index-on-demand-and-reclaimable.md) | KASAN 6.12.112; ext4, btrfs, XFS |
| SELinux em modo enforcing; checagem por resultado quando um LSM rotula os inodes da camada | [0104](docs/history/0104-selinux-investigation.md) | 6.12.112 com a política de referência do Debian |
| SQL sobre um domínio por uma tabela virtual do SQLite (protótipo) | [0108](docs/history/0108-sqlite-virtual-table-prototype.md), [ADR-0013](docs/decisions/0013-user-space-query-library.md) | KASAN 6.12.112; ext4 |
| Descrições do syzkaller para a montagem, as ioctls e os atributos; três rodadas de seis horas | [0109](docs/history/0109-syzkaller-first-runs.md), [0112](docs/history/0112-mount-leak-and-sql-omit.md), [0133](docs/history/0133-syzkaller-third-run.md) | 6.12.112 upstream com KCOV |
| Correções de segurança dos achados F9 a F12 (regra de homogeneidade, checagens sem o semáforo de consulta, acesso de escrita na montagem de baixo, lower idmapped recusado), cada uma com teste e rodada negativa | [0116](docs/history/0116-homogeneity-classes-and-checks-without-query-sem.md), [0117](docs/history/0117-lower-mount-write-access.md) | KASAN 6.12.112; ext4, btrfs, XFS |

Não implementado: índice persistente, montagens em namespace de usuário ou idmapped, leituras em
lote, cursor de mudanças, revisões, empacotamento, UAPI estável, CLI, a suíte xfstests completa e
o método M4 completo (só existem campanhas de nível piloto, em
[bench/results/](bench/results/README.md); veja [O que foi medido](#o-que-foi-medido)). Veja o
[ROADMAP.md](ROADMAP.md) para os itens planejados e os que dependem de validação.

## Comparado com arquivos comuns, SQLite e frameworks de memória

**Arquivos comuns.** Mesmo modelo de dados (arquivos mais xattrs), sem módulo, sem root,
portabilidade total; as consultas são `find` mais `getfattr`, uma varredura linear sem chamada de
consulta. Se consultas por atributo são raras, arquivos comuns vencem.

**SQLite ao lado dos arquivos.** SQL completo, índices compostos, décadas de maturidade, falhas
contidas num processo, transações atômicas de dados e metadados dentro do banco; a aplicação
implementa a autorização e mantém arquivos e linhas coerentes. Hoje esta é a recomendação para
qualquer coisa fora de um laboratório.

**Frameworks de memória (mem0, Letta, ai-memory).** Eles decidem o que lembrar e como montar um
prompt; o MoobFS guarda arquivos e responde a predicados de atributo. São clientes possíveis em
cima da camada, não concorrentes; o plano M4 os usa como cargas de trabalho.

**MoobFS.** Um só armazenamento, autorização por resultado feita pelo SO, sem daemon, dados
legíveis sem o módulo; em troca, um módulo de kernel de um autor, um kernel testado, um índice só
em RAM e uma vantagem medida que é estreita e de nível piloto
([O que foi medido](#o-que-foi-medido)). Tabela de comparação completa e as condições em que pode
valer a pena avaliar: [when-not-to-use.md](docs/when-not-to-use.md).

## Mapa da documentação

Todos os documentos em `docs/`, com o idioma, estão listados no [índice da documentação](docs/README.md).
Os documentos estão em inglês (ADR-0010).

| Pergunta | Documento |
| --- | --- |
| Como a camada é construída, com citações do código? | [architecture-overview.md](docs/architecture-overview.md) |
| Devo usar, comparado com arquivos, SQLite, FUSE e frameworks de memória? | [when-not-to-use.md](docs/when-not-to-use.md), [value-assessment.md](docs/value-assessment.md) |
| Respostas curtas | [faq.md](docs/faq.md) |
| Como compilo, testo e leio os logs? | [development.md](docs/development.md), [testing.md](docs/testing.md) |
| Como uso as ferramentas de linha de comando? | [tools.md](docs/tools.md), páginas de manual em [man/](man/) (`man ./man/moobfs-attr.1`) |
| O que a camada M2 especifica? | [spec-m2.md](docs/spec-m2.md), [kernel-infrastructure.md](docs/kernel-infrastructure.md), [ADR-0007](docs/decisions/0007-stacked-memory-layer.md) |
| O que o M3 especifica (identidade, isolamento, proveniência, cursor de mudanças, leituras em lote)? | [spec-m3.md](docs/spec-m3.md) |
| Como o desempenho será medido? | [benchmark-plan.md](docs/benchmark-plan.md), [benchmark-harness.md](docs/benchmark-harness.md) |
| Do que um sistema de memória precisa para usar a camada, e o que ele arrisca? | [client-contract.md](docs/client-contract.md), [adoption-risks.md](docs/adoption-risks.md) |
| O que foi medido até agora (nível piloto)? | [Relatório M4](docs/history/0088-m4-report.md); [bench/results/](bench/results/README.md); arquivos brutos no release [`bench-evidence`](https://github.com/studiomoob/llmoobfs/releases/tag/bench-evidence) |
| O que o `QUERY2` limitado por domínio (UAPI v2) especifica? | [spec-query-v2.md](docs/spec-query-v2.md) |
| Qual é o formato de imagem do M1? | [format-m1.md](docs/format-m1.md) |
| Qual era a proposta original completa do MVP? | [native-mvp.md](docs/native-mvp.md) (interface de escrita substituída pelo ADR-0007) |
| Por que cada decisão foi tomada? | [docs/decisions/](docs/decisions/), [diário técnico](docs/history/) |
| Que software livre é relevante em volta do projeto? | [ecosystem.md](docs/ecosystem.md) |
| Como o desenvolvimento assistido por IA é governado? | [ai-workflow.md](docs/ai-workflow.md) (compartilhado por `AGENTS.md`, `CLAUDE.md` e regras do Cursor) |
| Trabalhos anteriores, o que pode e o que não pode ser afirmado | [research-originality.md](docs/research-originality.md) |
| Que kernels são suportados, e o que pode mudar entre versões? | [compatibility.md](docs/compatibility.md) |
| Como posso ajudar? | [help-wanted.md](docs/help-wanted.md) |
| Mudanças, governança, conduta, segurança | [CHANGELOG.md](CHANGELOG.md), [GOVERNANCE.md](GOVERNANCE.md), [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md), [SECURITY.md](SECURITY.md) |

O Git registra as mudanças exatas; os diários registram justificativas concisas, experimentos e
resultados. O trabalho futuro precisa distinguir propostas de comportamento implementado e
testado.

## Trabalhos anteriores e posicionamento

O MoobFS se apoia em trabalhos que reconhece: o Semantic File System (1991) e o Be File System
(1997), pelas consultas por atributo; o overlayfs e o ecryptfs, pelo modelo de sistema de arquivos
empilhado; e o YoloFS (2026), que já demonstra um módulo de kernel empilhado voltado a agentes. O
`QUERY2` limitado por domínio segue a pesquisa sobre busca com controle de acesso: particionar o
índice pelo conjunto de leitores (Singh et al. 2007; GUFI, SC22) fecha os vazamentos que filtrar
depois da busca deixa abertos (Büttcher e Clarke, FAST 2005; Wang et al., S&P 2017). O projeto não
reivindica nenhuma dessas ideias como nova; o que ele explora é a combinação delas numa camada
empilhada no kernel, e se isso tem valor mensurável
([research-originality.md](docs/research-originality.md) §4, §5, §9). Cada trabalho, projeto e
ferramenta usado está listado com fontes verificadas em [references.md](docs/references.md).

Obrigado a todos cujo trabalho publicado sustenta este projeto: os autores desses artigos e os
mantenedores do kernel, dos sistemas de arquivos e das ferramentas de teste citados ali.

## Contribuição e segurança

Relatos de bugs, revisão do formato, revisão de kernel e melhorias de reprodutibilidade são
bem-vindos. Leia o [CONTRIBUTING.md](CONTRIBUTING.md) (assinatura DCO, requisitos de teste, regras de
revisão). Perguntas vão para o GitHub Discussions ou para uma issue. Contribuições assistidas por
IA são bem-vindas com revisão humana e evidência de teste precisa. Não coloque detalhes privados
de vulnerabilidade em issues públicas; use o [contato de segurança](SECURITY.md).

## Como citar

Os metadados de citação estão em [CITATION.cff](CITATION.cff).

## Licença

O projeto inteiro é licenciado sob **GPL-2.0-only**. Veja [LICENSE](LICENSE).
