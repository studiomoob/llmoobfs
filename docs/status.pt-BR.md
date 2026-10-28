# Estado

> Espelho de cortesia de [status.md](status.md), que prevalece em caso de diferença
> ([ADR-0010](decisions/0010-english-everywhere.md)).

O estado atual do MoobFS com mais detalhe que o aviso do README. Até 2026-10-25 o texto abaixo
abria o README; ele veio para cá com o item C1 do roadmap (diário 0197), para que quem chega
encontre primeiro o que o projeto é e só depois o histórico de estado. Os fatos não mudaram. A
seção [Estado](../README.pt-BR.md#estado) do README mantém a lista-resumo, e
[Limitações](../README.pt-BR.md#limitações), os limites.

## Última tag e correções posteriores

**Experimental. Não é para produção.** Uma série de kernel (6.12), testada só em VM; ainda sem
revisão humana independente de todo o código do kernel (duas revisões independentes do código, feitas por
assistentes de IA, diários 0111 e 0114; um revisor externo relatou em privado três defeitos do kernel e
escreveu a correção de um deles, F16, diários 0215, 0216 e 0218; um colaborador de fora revisou scripts,
ferramentas e a biblioteca Python com um assistente de IA, pull requests 161 a 163). A última tag, `v0.4.1-experimental` (2026-10-20), é um retrato do
código anterior às correções que hoje estão na `main`. Os dois achados da
[revisão independente](history/0144-independent-review.md) têm correções na
`main`. Esse lote passou na suíte completa integrada em VM e na
KASAN/UBSAN/lockdep local; as verificações obrigatórias do PR passaram antes do merge.
A `v0.4.1-experimental` ainda contém
ambos os defeitos. Uma revisão posterior (diário 0151) achou um terceiro, um chmod durante a
construção do índice que deixava o arquivo nas respostas do `QUERY2`; corrigido no diário 0152.
Veja [Limitações](../README.pt-BR.md#limitações).

As correções posteriores para renomeação de domínio e orçamento do índice
foram integradas pela [PR 126](https://github.com/studiomoob/llmoobfs/pull/126)
após suas 11 verificações passarem; as issues 110 e 111 foram fechadas. Uma
rodada KVM nativa posterior encontrou outra recusa concorrente de orçamento.
A tentativa de liberar outro domínio elegível quando o mais antigo está em
construção, junto com o teste restante de falha de alocação sem privilégios
(T9), entrou pela [PR 127](https://github.com/studiomoob/llmoobfs/pull/127)
após as onze verificações passarem, inclusive KASAN/lockdep ([diários 0178](history/0178-unprivileged-allocation-fault-queries.md)
e [0179](history/0179-kvm-overbudget-race-followup.md)). A issue 114 foi
fechada. A correção da cobrança ao cgroup de memória entrou pela
[PR 129](https://github.com/studiomoob/llmoobfs/pull/129) após as onze
verificações passarem, e a issue 116 foi fechada. A proteção experimental
de custo da issue 118 entrou pela [PR 135](https://github.com/studiomoob/llmoobfs/pull/135)
após onze verificações verdes; a issue e a divisão justa S9 continuam
abertas ([diário 0182](history/0182-query-build-resource-cost-investigation.md),
[roadmap](../ROADMAP.md)). Ainda não houve comparação controlada de desempenho
antes e depois da cobrança ao cgroup de memória ([diário 0180](history/0180-memcg-accounting-investigation.md)).
A opção de limite de trabalho por montagem entrou depois pela
[PR 138](https://github.com/studiomoob/llmoobfs/pull/138), com as doze
verificações aprovadas ([diário 0193](history/0193-merged-per-mount-build-work-policy.md));
a issue 118 continua aberta para ampliar as evidências.
*Atualização, 27/10/2026 ([diário 0210](history/0210-security-batch-1-domain-records-and-slow-lower.md)):*
a issue 118 foi fechada. Com um disco inferior ext4 atrasado por `dm-delay`, o primeiro build de um
domínio de 5.000 arquivos levou 0,3 s sem atraso e 9,8 s com 200 ms por E/S; com 400 ms parou no
prazo com `ETIMEDOUT`, sem resposta parcial, e a consulta seguinte respondeu completa. Os domínios
de cerca de 25 mil memórias reais do G5 se construíram com `index_build_work=65536`. A issue 113
(registros de domínio mantidos até o umount) foi corrigida no mesmo lote.
*Atualização, 28/10/2026 ([diário 0219](history/0219-security-batch-checks.md)):* um segundo lote
de segurança corrige três relatos privados de um revisor externo, cada um com teste e rodada
negativa: o `QUERY2` nomeando arquivos sob um diretório só de busca (F15 do ADR-0011,
[GHSA-95h9-j7c2-gcw6](https://github.com/studiomoob/llmoobfs/security/advisories/GHSA-95h9-j7c2-gcw6), [diário 0215](history/0215-search-only-directories.md)),
registros de domínio e de arquivo respondidos para um número de inode reutilizado
([GHSA-756j-cgwq-gvwm](https://github.com/studiomoob/llmoobfs/security/advisories/GHSA-756j-cgwq-gvwm), [diário 0216](history/0216-domain-record-generation.md))
e um bit set-user-ID mantido depois de uma escrita (F16, [GHSA-6xf4-r246-92r6](https://github.com/studiomoob/llmoobfs/security/advisories/GHSA-6xf4-r246-92r6),
[diário 0218](history/0218-write-credentials-fsetid.md)). A `v0.4.1-experimental` contém os três.
*Atualização, 28/10/2026 (diários 0221 a 0224):* um terceiro lote corrige o achado F17
([GHSA-82w5-ph62-47wf](https://github.com/studiomoob/llmoobfs/security/advisories/GHSA-82w5-ph62-47wf), [diário 0222](history/0222-term-padding-admin-test-extended-acl.md)) e
fecha as issues 119 e 115. Issue 119: um diretório de baixo com casefold é recusado com `EREMOTE`
(`ESTALE` se ganhou casefold por trás da camada) e um subvolume btrfs com `EXDEV`, e as recusas
são registradas no log uma vez só ([diário 0221](history/0221-lookup-errnos-and-m8.md)); o byte
reservado de um termo de consulta tem de ser zero (`EINVAL`) e um só teste de privilégio guarda o
índice ([ADR-0026](decisions/0026-ioctl-magic-term-padding-and-admin-test.md)). Issue 115:
`posix_fadvise`, `readahead`, os campos de `statx` do sistema de baixo, o espaço de um arquivo
apagado por trás da camada e o `O_NOATIME` ligado por `F_SETFL` agora chegam ao sistema de baixo;
`cachestat(2)` continua uma limitação ([diário 0224](history/0224-passthrough-gaps-issue-115.md)). A
issue 117 foi corrigida em parte: criar e despejar um inode sem registro no índice não toma mais a
trava do índice da montagem inteira; um inode com registro ainda toma, e a issue continua aberta
para ele ([diário 0223](history/0223-index-lock-instantiation-eviction.md)). A `v0.4.1-experimental`
contém todos esses defeitos.
