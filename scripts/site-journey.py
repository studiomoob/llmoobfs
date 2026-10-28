#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Generates the journey pages of the MoobFS site in English and Portuguese (ADR-0015).
Usage: python3 scripts/site-journey.py . from the repository root. Writes
site/{journey,status,research,how}.html and site/pt/ with the same pages in Portuguese; the
milestones, the status answers and the research topics are edited here, in both languages."""
import html, sys
from pathlib import Path

ROOT = Path(sys.argv[1])
GH = 'https://github.com/studiomoob/llmoobfs/blob/main/'
BASE = 'https://studiomoob.github.io/llmoobfs/'

def j(n, title=None):
    name = {p.name[:4]: p.name for p in (ROOT / 'docs/history').glob('0*.md')}[n]
    return f'<a href="{GH}docs/history/{name}">{title or "journal " + n}</a>'

def adr(n):
    name = {p.name[:4]: p.name for p in (ROOT / 'docs/decisions').glob('0*.md')}[n]
    return f'<a href="{GH}docs/decisions/{name}">ADR-{n}</a>'

# (date, en, pt, journals)
MILESTONES = [
 ('2026-10-10', 'The question: can the filesystem carry an agent\'s memory fields and answer queries on them? A native read-only module comes first.',
  'A pergunta: o sistema de arquivos pode carregar os campos de memória de um agente e responder consultas sobre eles? Primeiro vem um módulo nativo, só de leitura.', ['0001', '0002']),
 ('2026-10-10', f'Change of course: a stacked layer over ext4, btrfs and XFS instead of a new on-disk format ({adr("0007")}).',
  f'Mudança de rumo: uma camada empilhada sobre ext4, btrfs e XFS em vez de um formato novo em disco ({adr("0007")}).', ['0010', '0011', '0012']),
 ('2026-10-10', 'Typed attributes validated in the kernel; the first in-memory index and attribute query.',
  'Atributos tipados validados no kernel; o primeiro índice em memória e a primeira consulta por atributo.', ['0013', '0014']),
 ('2026-10-10', 'Our own KASAN and lockdep kernel, from the start.',
  'Kernel próprio com KASAN e lockdep, desde o começo.', ['0016']),
 ('2026-10-11', f'Queries scoped by directory and authorized by file permissions: per-domain partitions and the homogeneity rule ({adr("0009")}).',
  f'Consultas por diretório, autorizadas pelas permissões dos arquivos: partições por domínio e a regra de homogeneidade ({adr("0009")}).', ['0034', '0035']),
 ('2026-10-12', 'Measured against SQLite on the same machine: faster on filtered conjunctions, slower on long ordered traversals.',
  'Medido contra SQLite na mesma máquina: mais rápido em consultas filtradas, mais lento em percursos ordenados longos.', ['0047']),
 ('2026-10-12', f'A written threat and credential model, the contract every review is judged against ({adr("0011")}).',
  f'Um modelo de ameaças e de credenciais escrito, o contrato contra o qual toda revisão é julgada ({adr("0011")}).', ['0060']),
 ('2026-10-12', 'A security finding fixed: a write to a set-user-ID file crashed the kernel.',
  'Um achado de segurança corrigido: uma escrita num arquivo set-user-ID derrubava o kernel.', ['0069']),
 ('2026-10-13', 'A real memory client on the layer: equal answers from 150 to 60 000 memories.',
  'Um cliente de memória real sobre a camada: respostas iguais de 150 a 60 000 memórias.', ['0068', '0088']),
 ('2026-10-13', 'fstests\' quick group passes over the three lower filesystems; systematic allocation-failure injection.',
  'O grupo rápido do fstests passa sobre os três sistemas de arquivos de baixo; injeção sistemática de falha de alocação.', ['0091', '0096']),
 ('2026-10-13', 'The alternative without a kernel module, measured honestly; the module stays, its costs go.',
  'A alternativa sem módulo do kernel, medida com honestidade; o módulo fica, os custos dele saem.', ['0094', '0095']),
 ('2026-10-13', 'First answers from the kernel community, and the decisions that followed.',
  'As primeiras respostas da comunidade do kernel, e as decisões que vieram delas.', ['0097']),
 ('2026-10-14', f'An index built on demand and released under memory pressure ({adr("0012")}).',
  f'Um índice montado sob demanda e liberado sob pressão de memória ({adr("0012")}).', ['0098', '0101']),
 ('2026-10-14', 'SELinux shows the limit of the homogeneity rule; fixed.',
  'O SELinux mostra o limite da regra de homogeneidade; corrigido.', ['0104']),
 ('2026-10-14', f'SQL over a domain through a SQLite virtual table ({adr("0013")}).',
  f'SQL sobre um domínio por uma tabela virtual do SQLite ({adr("0013")}).', ['0108']),
 ('2026-10-14', 'syzkaller over the layer: a reference leak found and fixed; two six-hour runs.',
  'syzkaller sobre a camada: um vazamento de referência achado e corrigido; duas rodadas de seis horas.', ['0109', '0112']),
 ('2026-10-19', 'A measurement method with intervals finds a roughly 2x query regression; reusing the query buffer fixes it.',
  'Um método de medição com intervalos encontra uma regressão de aproximadamente 2x nas consultas; reutilizar o buffer da consulta a corrige.', ['0134', '0139']),
 ('2026-10-20', 'v0.4.1-experimental published; a fifth six-hour syzkaller run reports no crash and reaches 88.7% aggregate handler coverage, with xattr-get coverage still unexplained.',
  'v0.4.1-experimental publicada; a quinta rodada de seis horas do syzkaller termina sem relato de crash e chega a 88,7% de cobertura agregada dos handlers, ainda sem explicar a cobertura de xattr-get.', ['0142', '0143']),
 ('2026-10-25', f'Unprivileged domain builds gain experimental work, time and frequency limits, then a per-mount index_build_work choice (default 32,768; range 1,024–65,536). Issue 118 stays open ({adr("0019")}, {adr("0020")}).',
  f'Construções de domínio sem privilégio ganham limites experimentais de trabalho, tempo e frequência e, em seguida, a opção index_build_work por montagem (padrão 32.768; intervalo 1.024–65.536). A issue 118 continua aberta ({adr("0019")}, {adr("0020")}).', ['0182', '0193']),
 ('2026-10-27', 'Issue 118 closed: over a lower slowed to 400 ms per I/O the first build of a domain stops at its deadline with no partial answer.',
  'Issue 118 fechada: com um disco de baixo atrasado em 400 ms por E/S, o primeiro build de um domínio para no prazo, sem resposta parcial.', ['0210']),
 ('2026-10-28', 'Three kernel defects reported privately by an external reviewer are fixed through private advisories: search-only directories (F15), records of a reused inode number, set-user-ID kept through a write (F16, the reviewer\'s own fix).',
  'Três defeitos do kernel relatados em privado por um revisor externo são corrigidos por avisos privados: diretórios só de busca (F15), registros de um número de inode reutilizado, set-user-ID mantido numa escrita (F16, correção do próprio revisor).', ['0215', '0216', '0218', '0219']),
]

T = {
 'en': dict(lang='en', other='pt', other_label='Português', home='index.html', prefix='',
   nav=[('guide.html', 'Documentation'), ('plan.html', 'Plan'), ('journey.html', 'Journey'), ('faq.html', 'FAQ')],
   journey_title='Journey', journey_desc='How MoobFS got here, milestone by milestone, from the first question to the current tests, with every step linked to its journal.',
   journey_lead='Every work session of the project is recorded in a journal: what was asked, what was decided, what was tested and what failed. This page picks the milestones. The full list is the journal index.',
   full='The full journal index', full_note='journals are in English',
   status_title='Status', status_desc='Where MoobFS stands against what the Linux kernel asks of a new filesystem: tests, maintainers, users, and what is still missing.',
   status_lead='The kernel documents what it expects before a new filesystem is considered (Documentation/filesystems/adding-new-filesystems.rst). MoobFS is out of tree and experimental; this page answers those questions honestly, as of the date below.',
   research_title='Open research topics', research_desc='Questions MoobFS raises that would make a good undergraduate project, master\'s thesis or workshop paper.',
   research_lead='Questions this project raises and does not answer yet. Each one comes with what exists to start from. If one interests you or a student, write to us.',
   how_title='How this project is made', how_desc='One maintainer, AI coding assistants, and a public journal of every decision and test.',
   updated='Updated'),
 'pt': dict(lang='pt-BR', other='en', other_label='English', home='../index.html', prefix='../',
   nav=[('../guide.html', 'Documentação (inglês)'), ('../plan.html', 'Plano (inglês)'), ('journey.html', 'Trajetória'), ('../faq.html', 'Perguntas (inglês)')],
   journey_title='Trajetória', journey_desc='Como o MoobFS chegou até aqui, marco a marco, da primeira pergunta aos testes de hoje, com cada passo ligado ao seu diário.',
   journey_lead='Cada sessão de trabalho do projeto fica registrada num diário: o que foi pedido, o que se decidiu, o que foi testado e o que deu errado. Esta página escolhe os marcos. A lista completa é o índice dos diários.',
   full='O índice completo dos diários', full_note='os diários são em inglês',
   status_title='Situação', status_desc='Onde o MoobFS está diante do que o kernel Linux pede de um sistema de arquivos novo: testes, mantenedores, usuários e o que ainda falta.',
   status_lead='O kernel documenta o que espera antes de considerar um sistema de arquivos novo (Documentation/filesystems/adding-new-filesystems.rst). O MoobFS está fora da árvore e é experimental; esta página responde a essas perguntas com honestidade, na data abaixo.',
   research_title='Temas de pesquisa em aberto', research_desc='Perguntas que o MoobFS levanta e que dariam um bom TCC, uma dissertação de mestrado ou um artigo de workshop.',
   research_lead='Perguntas que o projeto levanta e ainda não responde. Cada uma vem com o que já existe para começar. Se alguma interessar a você ou a um aluno, escreva para a gente.',
   how_title='Como o projeto é feito', how_desc='Um mantenedor, assistentes de IA para programação e um diário público de cada decisão e teste.',
   updated='Atualizado em'),
}

UPDATED = '2026-10-25'

STATUS = [
 ('What does it do that existing filesystems do not? Why is FUSE not enough?',
  'O que ele faz que os sistemas de arquivos existentes não fazem? Por que FUSE não basta?',
  f'Typed attributes validated by the kernel, an in-kernel index of them, and a query authorized by file permissions, over an unchanged ext4, btrfs or XFS tree. FUSE was considered and rejected for this design ({adr("0001")}); a user-space index kept by inotify was built and measured as the alternative ({j("0094")}).',
  f'Atributos tipados validados pelo kernel, um índice deles dentro do kernel e uma consulta autorizada pelas permissões dos arquivos, sobre uma árvore ext4, btrfs ou XFS que não muda. FUSE foi considerado e descartado para este desenho ({adr("0001")}); um índice em espaço de usuário mantido por inotify foi construído e medido como alternativa ({j("0094")}).'),
 ('Modern kernel interfaces (iomap, folios)', 'Interfaces modernas do kernel (iomap, folios)',
  'Not applicable to the layer: it has no block mapping of its own and delegates data I/O to the lower filesystem through the kernel\'s backing-file helpers, as overlayfs does.',
  'Não se aplica à camada: ela não mapeia blocos e delega a E/S de dados ao sistema de arquivos de baixo pelas funções de backing file do kernel, como o overlayfs.'),
 ('mkfs and fsck', 'mkfs e fsck',
  'No on-disk format of its own. moobfs-check verifies the typed attributes of a tree and lists the files left out of query answers.',
  'Não há formato próprio em disco. O moobfs-check verifica os atributos tipados de uma árvore e lista os arquivos que ficam fora das respostas.'),
 ('fstests', 'fstests',
  f'The generic quick group passes on ext4, btrfs and XFS below the layer, with a documented exclusion list ({j("0091")}).',
  f'O grupo generic rápido passa com ext4, btrfs e XFS embaixo da camada, com uma lista de exclusões documentada ({j("0091")}).'),
 ('Sanitizers and fuzzing', 'Sanitizers e fuzzing',
  f'KASAN, UBSAN and lockdep on every pull request that touches the module (required before merge); allocation-failure injection inside the module ({j("0096")}); syzkaller, five six-hour runs, the second to fifth with no report ({j("0109")}, {j("0133")}, {j("0138")}, {j("0143")}).',
  f'KASAN, UBSAN e lockdep em todo pull request que mexe no módulo (obrigatório antes do merge); injeção de falha de alocação dentro do módulo ({j("0096")}); syzkaller, cinco rodadas de seis horas, da segunda à quinta sem nenhum relatório ({j("0109")}, {j("0133")}, {j("0138")}, {j("0143")}).'),
 ('Maintainers', 'Mantenedores',
  'One. The kernel asks for two or more: this is the main reason we are looking for collaborators.',
  'Um. O kernel pede dois ou mais: é o principal motivo de estarmos procurando colaboradores.'),
 ('Users', 'Usuários',
  f'Test clients only: an agent-memory tool and a LangGraph store, both with answers equal to their own storage ({j("0088")}, {j("0089")}). No production users; it is not ready for them.',
  f'Só clientes de teste: uma ferramenta de memória para agentes e um store do LangGraph, os dois com respostas iguais às do armazenamento deles ({j("0088")}, {j("0089")}). Nenhum usuário em produção; o projeto não está pronto para isso.'),
 ('Kernel versions and architectures', 'Versões do kernel e arquiteturas', f'One series, 6.12, on x86-64, in virtual machines. The attribute encoding and the image tools also pass their tests on big-endian s390x under emulation, on every pull request; on arm64, under emulation and without KVM, the module passes the ext4, btrfs and XFS steps under KASAN in an exploratory job ({j("0137")}).', f'Uma série, 6.12, em x86-64, em máquinas virtuais. A codificação de atributos e as ferramentas de imagem também passam nos testes em s390x big-endian, com emulação, em todo pull request; em arm64, com emulação e sem KVM, o módulo passa nos passos de ext4, btrfs e XFS sob KASAN num job exploratório ({j("0137")}).'),
 ('Corrections from the 2026-10-20 review', 'Correções da revisão de 2026-10-20',
  f'The corrections are on main. They synchronize backing-file flags after F_SETFL (R1) and propagate operational getxattr errors (R2). v0.4.1-experimental still contains both original defects. The full integrated VM suite at 49098aa passed: clearing and setting append on ext4, btrfs and XFS, the injected-error regression, and acceptance/layer suites. Negative rounds detected both defects. The full local KASAN/UBSAN/lockdep suite also passed on Linux 6.12.112, and the required pull-request checks passed before that batch merged. Injected results do not reproduce a real lower-I/O fault (<a href="{GH}docs/history/0149-sync-backing-file-flags.md">R1</a>, <a href="{GH}docs/history/0150-propagate-index-xattr-read-errors.md">R2</a>).',
  f'As correções estão na main. Elas sincronizam flags do arquivo de baixo após F_SETFL (R1) e propagam erros operacionais de getxattr (R2). A v0.4.1-experimental ainda contém ambos os defeitos originais. A suíte completa integrada em VM do commit 49098aa passou: limpar e ativar append em ext4, btrfs e XFS, regressão com erros injetados e suítes de aceitação e da camada. As negativas detectaram ambos os defeitos. A suíte local completa sob KASAN/UBSAN/lockdep também passou no Linux 6.12.112, e as verificações obrigatórias do pull request passaram antes do merge desse lote. Resultados injetados não reproduzem falha real de E/S de baixo (<a href="{GH}docs/history/0149-sync-backing-file-flags.md">R1</a>, <a href="{GH}docs/history/0150-propagate-index-xattr-read-errors.md">R2</a>).'),
 ('Unprivileged query cost', 'Custo de consulta sem privilégio',
  f'An authorized QUERY2 can build a domain. main bounds that build with index_build_work (default 32,768 units; a mounter may choose 1,024–65,536), a ten-second cooperative deadline and rebuild-frequency limits. The default denies the measured 17,000-file domain. Issue 118 is closed: over a lower slowed to 400 ms per I/O the build stops at the deadline with no partial answer ({j("0210")}). These are not strict CPU or physical-I/O caps, and they are not fair memory shares ({adr("0019")}, {adr("0020")}, {j("0193")}).',
  f'Um QUERY2 autorizado pode construir um domínio. A main limita essa construção com index_build_work (padrão de 32.768 unidades; quem monta pode escolher de 1.024 a 65.536), um prazo cooperativo de dez segundos e limites de frequência de reconstrução. O padrão recusa o domínio medido de 17 mil arquivos. A issue 118 foi fechada: com um disco de baixo atrasado em 400 ms por E/S, a construção para no prazo sem resposta parcial ({j("0210")}). Isso não é teto estrito de CPU ou de E/S física, nem divisão justa de memória ({adr("0019")}, {adr("0020")}, {j("0193")}).'),
 ('Security', 'Segurança',
  f'A written threat model ({adr("0011")}) with every finding recorded; private reporting through <a href="{GH}SECURITY.md">SECURITY.md</a>. An external reviewer privately reported three kernel defects, fixed in {j("0215")}, {j("0216")} and {j("0218")}; there is no independent human review of the whole kernel code.',
  f'Um modelo de ameaças escrito ({adr("0011")}) com cada achado registrado; relato privado pelo <a href="{GH}SECURITY.md">SECURITY.md</a>. Um revisor externo relatou em privado três defeitos do kernel, corrigidos nos {j("0215")}, {j("0216")} e {j("0218")}; não há revisão humana independente de todo o código do kernel.'),
]

RESEARCH = [
 ('Reader-set indexing with security labels', 'Índice por conjunto de leitores com rótulos de segurança',
  f'The query answers from a per-directory partition when "everyone who can traverse can read". Mode bits and ACLs support a sufficient condition; security labels (SELinux, Smack) do not fit it yet, so labelled systems fall back to per-result checks ({j("0104")}). Is there a sound condition that brings labels into the partition?',
  f'A consulta responde a partir de uma partição por diretório quando "todo mundo que atravessa consegue ler". Bits de modo e ACLs dão uma condição suficiente; rótulos de segurança (SELinux, Smack) ainda não cabem nela, e sistemas com rótulos voltam à checagem resultado a resultado ({j("0104")}). Existe uma condição correta que traga os rótulos para dentro da partição?',
  'Security, databases', 'Segurança, banco de dados'),
 ('Ordered traversals in a kernel index', 'Percursos ordenados num índice do kernel',
  f'The optimization of {j("0048")} moved the short "N most recent" queries ahead of SQLite in that pilot. Long ordered traversals from the real client still lose, at about half the speed of SQLite at 60 000 memories ({j("0068")}, {j("0088")}). Which structure improves that measured case within the memory budget?',
  f'A otimização do {j("0048")} colocou as consultas curtas "N mais recentes" à frente do SQLite naquele piloto. Os percursos ordenados longos do cliente real ainda perdem, com cerca de metade da velocidade do SQLite em 60 000 memórias ({j("0068")}, {j("0088")}). Que estrutura melhora esse caso medido dentro do orçamento de memória?',
  'Data structures, systems', 'Estruturas de dados, sistemas'),
 ('Kernel module or user-space index?', 'Módulo do kernel ou índice em espaço de usuário?',
  f'The same queries were measured against a per-domain SQLite index kept by inotify ({j("0092")}, {j("0094")}). Under which workloads does each win, and what does consistency cost on each side?',
  f'As mesmas consultas foram medidas contra um índice SQLite por domínio mantido por inotify ({j("0092")}, {j("0094")}). Em que cargas cada um ganha, e quanto custa a consistência de cada lado?',
  'Operating systems, evaluation', 'Sistemas operacionais, avaliação'),
 ('Measurement uncertainty in filesystem evaluation', 'Incerteza de medição na avaliação de sistemas de arquivos',
  f'The early SQLite and client comparisons are pilots. The later module comparison uses 40 repetitions, held CPU frequency and confidence intervals ({j("0134")}, {j("0139")}, {j("0143")}). Repeating the SQLite comparisons with that method and reproducing them on other hardware remain open.',
  f'As comparações iniciais com SQLite e com o cliente são pilotos. A comparação posterior entre módulos usa 40 repetições, frequência de CPU fixa e intervalos de confiança ({j("0134")}, {j("0139")}, {j("0143")}). Repetir as comparações com SQLite com esse método e reproduzi-las em outro hardware continua em aberto.',
  'Performance evaluation', 'Avaliação de desempenho'),
 ('Benchmarks for agent memory at the storage layer', 'Benchmarks de memória de agentes na camada de armazenamento',
  'Agent-memory papers benchmark recall quality; few measure the storage queries a memory system issues. A reproducible workload from real clients would be useful beyond this project.',
  'Os artigos de memória para agentes medem a qualidade da recuperação; poucos medem as consultas de armazenamento que um sistema de memória faz. Uma carga reproduzível, tirada de clientes reais, seria útil além deste projeto.',
  'AI systems, benchmarking', 'Sistemas de IA, benchmarks'),
 ('The obligations of a stacked filesystem', 'As obrigações de um sistema de arquivos empilhado',
  f'Credentials, remount through the new mount API, the lower filesystem\'s own rules: a stacked filesystem inherits obligations that are scattered across overlayfs\'s history. A checked list, with tests, would help every stacked filesystem ({j("0099")}).',
  f'Credenciais, remount pela API nova de montagem, as regras do próprio sistema de arquivos de baixo: um sistema de arquivos empilhado herda obrigações espalhadas pela história do overlayfs. Uma lista conferida, com testes, ajudaria todo sistema de arquivos empilhado ({j("0099")}).',
  'Operating systems, security', 'Sistemas operacionais, segurança'),
]

HOW = {
 'en': f'''<p>MoobFS has one maintainer, a software developer, who works with AI coding assistants. Every change is reviewed and signed off by a human; nothing is merged without the KASAN and lockdep run passing.</p>
<p>Every work session is recorded in a <a href="{GH}docs/history/README.md">journal</a>: what was asked, what was decided and why, what was tested with which command, what failed, and what is still open. Architecture decisions are kept as numbered <a href="{GH}docs/decisions/">ADRs</a>, and a decision that changes is pointed to by its successor, never rewritten. Tests that were not run are never reported as passed; a fix comes with a "negative round" that shows its test failing without it.</p>
<p>We say this up front because the kernel community is discussing how to handle AI-assisted work, and we would rather be judged on the record than on a guess. The journals are that record.</p>
<p>Corrections are welcome: if a journal states something wrong, open an issue and it will get a dated note.</p>''',
 'pt': f'''<p>O MoobFS tem um mantenedor, desenvolvedor de software, que trabalha com assistentes de IA para programação. Toda mudança é revisada e assinada por uma pessoa; nada entra sem que a rodada com KASAN e lockdep passe.</p>
<p>Cada sessão de trabalho fica registrada num <a href="{GH}docs/history/README.md">diário</a> (em inglês): o que foi pedido, o que se decidiu e por quê, o que foi testado e com qual comando, o que deu errado e o que segue em aberto. As decisões de arquitetura ficam em <a href="{GH}docs/decisions/">ADRs</a> numerados, e uma decisão que muda é apontada pela que a substitui, nunca reescrita. Teste que não rodou nunca aparece como aprovado; uma correção vem com uma "rodada negativa" que mostra o teste falhando sem ela.</p>
<p>Dizemos isso logo de cara porque a comunidade do kernel está discutindo como lidar com trabalho feito com ajuda de IA, e preferimos ser julgados pelo registro do que por suposição. Os diários são esse registro.</p>
<p>Correções são bem-vindas: se um diário afirma algo errado, abra uma issue e ele ganha uma nota datada.</p>''',
}

def page(lang, name, title, desc, body):
    t = T[lang]
    here = name
    other_href = (f'../{name}' if lang == 'pt' else f'pt/{name}')
    navs = ''.join(f'<a href="{h}"{" aria-current=\"page\"" if h.endswith(here) and not h.startswith("..") else ""}>{l}</a>' for h, l in t['nav'])
    navs += f'<a href="https://github.com/studiomoob/llmoobfs">GitHub ↗</a><a href="{other_href}" lang="{T[t["other"]]["lang"]}">{t["other_label"]}</a>'
    canon = BASE + ('pt/' if lang == 'pt' else '') + name
    alt_en = BASE + name; alt_pt = BASE + 'pt/' + name
    return f'''<!doctype html>
<html lang="{t['lang']}"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>{title} — MoobFS</title><meta name="description" content="{html.escape(desc)}"><link rel="canonical" href="{canon}"><link rel="alternate" hreflang="en" href="{alt_en}"><link rel="alternate" hreflang="pt-BR" href="{alt_pt}"><meta property="og:title" content="{title} — MoobFS"><meta property="og:description" content="{html.escape(desc)}"><meta property="og:type" content="website"><meta property="og:url" content="{canon}"><meta property="og:image" content="{BASE}social-preview.png"><meta name="theme-color" content="white"><link rel="icon" href="{t['prefix']}favicon.svg" type="image/svg+xml"><link rel="stylesheet" href="{t['prefix']}style.css"></head>
<body><a class="skip" href="#main">{'Skip to content' if lang == 'en' else 'Pular para o conteúdo'}</a><header><div class="wrap"><a class="brand" href="{t['home']}"><svg viewBox="0 0 36 36" fill="none" aria-hidden="true"><path d="M3 8h10v8h9M3 28h10v-8h9" stroke="currentColor" stroke-width="3"/><rect x="22" y="12" width="11" height="12" fill="currentColor"/></svg>MoobFS</a><nav aria-label="{'Main' if lang == 'en' else 'Principal'}">{navs}</nav></div></header>
<main id="main" class="wrap guide journey">
<p class="kicker">{t['updated']} {UPDATED}</p>
<h1>{title}</h1>
{body}
<nav class="journey-nav" aria-label="{'Journey pages' if lang == 'en' else 'Páginas da trajetória'}"><a href="journey.html">{T[lang]['journey_title']}</a><a href="status.html">{T[lang]['status_title']}</a><a href="research.html">{T[lang]['research_title']}</a><a href="how.html">{T[lang]['how_title']}</a></nav>
</main></body></html>
'''

def build(lang):
    t = T[lang]; L = 0 if lang == 'en' else 1
    out = ROOT / 'site' / ('pt' if lang == 'pt' else '')
    out.mkdir(parents=True, exist_ok=True)
    items = ''.join(f'<li><time datetime="{d}">{d}</time><p>{(en, pt)[L]}</p><p class="refs">{" · ".join(j(n) for n in js)}</p></li>' for d, en, pt, js in MILESTONES)
    jb = f'<p class="lead">{t["journey_lead"]}</p><ol class="timeline">{items}</ol><p><a href="{GH}docs/history/README.md">{t["full"]}</a> ({t["full_note"]}).</p>'
    (out / 'journey.html').write_text(page(lang, 'journey.html', t['journey_title'], t['journey_desc'], jb))
    rows = ''.join(f'<tr><th scope="row">{(q, qp)[L]}</th><td>{(a, ap)[L]}</td></tr>' for q, qp, a, ap in STATUS)
    hd = ('Question', 'Answer') if lang == 'en' else ('Pergunta', 'Resposta')
    sb = f'<p class="lead">{t["status_lead"]}</p><div class="table"><table><thead><tr><th scope="col">{hd[0]}</th><th scope="col">{hd[1]}</th></tr></thead><tbody>{rows}</tbody></table></div>'
    (out / 'status.html').write_text(page(lang, 'status.html', t['status_title'], t['status_desc'], sb))
    cards = ''.join(f'<section class="topic"><h2>{(h, hp)[L]}</h2><p>{(b, bp)[L]}</p><p class="refs">{(a, ap)[L]}</p></section>' for h, hp, b, bp, a, ap in RESEARCH)
    contact = ('Contact: open a <a href="https://github.com/studiomoob/llmoobfs/discussions">discussion</a> on GitHub; tell us which topic and your context (course, lab, thesis).') if lang == 'en' else ('Contato: abra uma <a href="https://github.com/studiomoob/llmoobfs/discussions">discussão</a> no GitHub (pode ser em português); conte qual tema e o seu contexto (disciplina, laboratório, TCC ou mestrado).')
    rb = f'<p class="lead">{t["research_lead"]}</p>{cards}<p>{contact}</p>'
    (out / 'research.html').write_text(page(lang, 'research.html', t['research_title'], t['research_desc'], rb))
    (out / 'how.html').write_text(page(lang, 'how.html', t['how_title'], t['how_desc'], HOW[lang]))

build('en'); build('pt')
print('ok')
