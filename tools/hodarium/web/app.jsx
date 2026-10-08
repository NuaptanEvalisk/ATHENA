// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
import React, { useEffect, useState } from 'react';
import { createRoot } from 'react-dom/client';
import { ActionIcon, Alert, Button, Code, Group, Loader, MantineProvider, Modal,
  PasswordInput, Stack, Table, Tabs, Text, TextInput, Title, Tooltip, createTheme } from '@mantine/core';
import { useForm } from '@mantine/form';
import { startAuthentication, startRegistration } from '@simplewebauthn/browser';
import { AlertCircle, KeyRound, LogOut, Plus, RefreshCw, Search, ShieldCheck, Trash2 } from 'lucide-react';
import '@mantine/core/styles.css';
import './style.css';
import logo from '../../athena-web-server/web/athena-icon.png';

const theme = createTheme({ primaryColor: 'teal', defaultRadius: 0,
  radius: { xs: 0, sm: 0, md: 0, lg: 0, xl: 0 },
  fontFamily: 'system-ui, sans-serif', headings: { fontFamily: 'system-ui, sans-serif' } });

async function api(path, body, headers = {}) {
  const response = await fetch(`/api/${path}`, { method: body === undefined ? 'GET' : 'POST',
    credentials: 'same-origin', headers: { 'Content-Type': 'application/json', ...headers },
    body: body === undefined ? undefined : JSON.stringify(body) });
  if (!response.ok) {
    const error = new Error(response.status === 403 ? 'Authentication or recent verification required.' : (await response.text()).trim());
    error.status = response.status;
    throw error;
  }
  return response.json();
}
function decodeState(envelope) {
  const bytes = Uint8Array.from(atob(envelope.payload.replace(/-/g, '+').replace(/_/g, '/')), c => c.charCodeAt(0));
  return JSON.parse(new TextDecoder().decode(bytes));
}

function App() {
  const [info, setInfo] = useState(null);
  const [state, setState] = useState(null);
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);
  const [pending, setPending] = useState(null);
  const [confirmation, setConfirmation] = useState(null);
  const [passkeys, setPasskeys] = useState([]);
  const [audit, setAudit] = useState([]);
  const [moreAudit, setMoreAudit] = useState(false);
  const [tab, setTab] = useState('members');
  const bootstrap = useForm({ initialValues: { credential: '' } });
  const lookup = useForm({ initialValues: { code: '' }, validate: {
    code: value => /^\d{8}$/.test(value) ? null : 'Enter the eight-digit device code.' } });

  async function refresh() {
    setInfo(await api('info'));
    try { setState(decodeState(await api('admin/state'))); }
    catch (e) { if (e.status === 403) setState(null); else throw e; }
  }
  async function run(action) {
    setBusy(true); setError('');
    try { await action(); } catch (e) { setError(e.message || 'Operation failed.'); }
    finally { setBusy(false); }
  }
  useEffect(() => { void run(refresh); }, []);

  async function authenticate() {
    const begin = await api('auth/login/start', {});
    const response = await startAuthentication({ optionsJSON: begin.options.publicKey });
    await api('auth/login/finish', response, { 'X-Hodarium-Ceremony': begin.ceremony });
    await refresh();
  }
  async function register() {
    const begin = await api('auth/register/start', { bootstrap: bootstrap.values.credential });
    bootstrap.reset();
    const response = await startRegistration({ optionsJSON: begin.options.publicKey });
    await api('auth/register/finish', response, { 'X-Hodarium-Ceremony': begin.ceremony });
    await refresh();
    setPasskeys(await api('admin/passkeys'));
  }
  async function selectTab(value) {
    setTab(value);
    if (value === 'passkeys') setPasskeys(await api('admin/passkeys'));
    if (value === 'audit') {
      const rows = await api('admin/audit'); setAudit(rows); setMoreAudit(rows.length === 100);
    }
  }
  async function confirm() {
    if (confirmation.kind === 'expel') {
      await api('admin/expel', { member: confirmation.value.id, expected_revision: state.revision });
      await refresh();
    } else {
      await api('admin/passkeys/delete', { id: confirmation.value.id });
      setState(null);
    }
    setConfirmation(null);
  }

  return <MantineProvider theme={theme} defaultColorScheme="auto">
    <header className="app-header">
      <Group gap="sm"><img src={logo} alt="" width="32" height="32" /><Title order={1}>ATHENA Hodarium</Title></Group>
      <Group gap="xs">
        {state && <Button variant="subtle" leftSection={<ShieldCheck size={18} />} disabled={busy} onClick={() => run(authenticate)}>Verify</Button>}
        <Tooltip label="Refresh"><ActionIcon size="lg" variant="subtle" aria-label="Refresh" loading={busy} onClick={() => run(refresh)}><RefreshCw size={18} /></ActionIcon></Tooltip>
        {state && <Tooltip label="Sign out"><ActionIcon size="lg" variant="subtle" aria-label="Sign out" disabled={busy} onClick={() => run(async () => { await api('auth/logout', {}); setState(null); })}><LogOut size={18} /></ActionIcon></Tooltip>}
      </Group>
    </header>
    <main>
      {error && <Alert color="red" icon={<AlertCircle size={18} />} mb="md" withCloseButton onClose={() => setError('')}>{error}</Alert>}
      {!info ? <Loader aria-label="Loading authority" /> : !state ? <section className="authentication">
        <Title order={2}>{info.initialized ? 'Administrator sign in' : 'Initialize administration'}</Title>
        {info.initialized ? <Button leftSection={<KeyRound size={18} />} loading={busy} onClick={() => run(authenticate)}>Sign in with a passkey</Button> :
          <form onSubmit={bootstrap.onSubmit(() => run(register))}><Stack>
            <PasswordInput label="Bootstrap credential" autoComplete="off" required {...bootstrap.getInputProps('credential')} />
            <Button type="submit" loading={busy} leftSection={<KeyRound size={18} />}>Register first passkey</Button>
          </Stack></form>}
        <Text size="sm" c="dimmed">Authority</Text><Code block>{info.authority_key}</Code>
      </section> : <>
        <Group justify="space-between" className="state-summary">
          <Text fw={600}>{state.members.length} admitted {state.members.length === 1 ? 'device' : 'devices'}</Text>
          <Text size="sm" c="dimmed">Membership revision {state.revision}</Text>
        </Group>
        <Tabs value={tab} onChange={value => run(() => selectTab(value))}>
          <Tabs.List><Tabs.Tab value="members">Devices</Tabs.Tab><Tabs.Tab value="passkeys">Passkeys</Tabs.Tab><Tabs.Tab value="audit">Audit</Tabs.Tab></Tabs.List>
          <Tabs.Panel value="members" pt="lg">
            <form onSubmit={lookup.onSubmit(async values => run(async () => { setPending(null); setPending(await api('admin/lookup', { code: values.code })); }))}>
              <Group align="flex-start"><TextInput label="Device verification code" inputMode="numeric" autoComplete="one-time-code" maxLength={8} {...lookup.getInputProps('code')} />
                <Button className="lookup-button" type="submit" leftSection={<Search size={18} />} loading={busy}>Find request</Button></Group>
            </form>
            {pending && <section className="pending-request"><Title order={3}>{pending.name}</Title>
              <Text size="sm">Device public key</Text><Code block>{pending.public_key}</Code>
              <Text size="sm" c="dimmed">Expires {new Date(pending.expires * 1000).toLocaleTimeString()}</Text>
              <Group><Button loading={busy} leftSection={<Plus size={18} />} onClick={() => run(async () => {
                await api('admin/admit', { id: pending.id, public_key: pending.public_key, expected_revision: state.revision });
                setPending(null); lookup.reset(); await refresh();
              })}>Admit device</Button><Button variant="subtle" onClick={() => setPending(null)}>Cancel</Button></Group>
            </section>}
            <Table.ScrollContainer minWidth={580}><Table mt="lg" verticalSpacing="sm">
              <Table.Thead><Table.Tr><Table.Th>Device</Table.Th><Table.Th>Member identity</Table.Th><Table.Th>Public key</Table.Th><Table.Th className="action-column">Actions</Table.Th></Table.Tr></Table.Thead>
              <Table.Tbody>{state.members.map(member => <Table.Tr key={member.id}><Table.Td>{member.name}</Table.Td><Table.Td><Code>{member.id}</Code></Table.Td><Table.Td><Code>{member.public_key}</Code></Table.Td>
                <Table.Td><Tooltip label={`Expel ${member.name}`}><ActionIcon variant="subtle" color="red" aria-label={`Expel ${member.name}`} disabled={busy} onClick={() => setConfirmation({ kind: 'expel', value: member })}><Trash2 size={18} /></ActionIcon></Tooltip></Table.Td></Table.Tr>)}</Table.Tbody>
            </Table></Table.ScrollContainer>
            {!state.members.length && <Text c="dimmed" mt="lg">No admitted devices.</Text>}
          </Tabs.Panel>
          <Tabs.Panel value="passkeys" pt="lg">
            <Button leftSection={<Plus size={18} />} loading={busy} onClick={() => run(register)}>Register passkey</Button>
            <Table.ScrollContainer minWidth={420}><Table mt="lg"><Table.Thead><Table.Tr><Table.Th>Credential identity</Table.Th><Table.Th className="action-column">Actions</Table.Th></Table.Tr></Table.Thead>
              <Table.Tbody>{passkeys.map(key => <Table.Tr key={key.id}><Table.Td><Code className="key-value">{key.id}</Code></Table.Td><Table.Td><Tooltip label={passkeys.length <= 1 ? 'Keep at least one passkey' : 'Remove passkey'}>
                <ActionIcon variant="subtle" color="red" aria-label="Remove passkey" disabled={busy || passkeys.length <= 1} onClick={() => setConfirmation({ kind: 'passkey', value: key })}><Trash2 size={18} /></ActionIcon>
              </Tooltip></Table.Td></Table.Tr>)}</Table.Tbody></Table></Table.ScrollContainer>
          </Tabs.Panel>
          <Tabs.Panel value="audit" pt="lg">
            <Table.ScrollContainer minWidth={660}><Table><Table.Thead><Table.Tr><Table.Th>Time</Table.Th><Table.Th>Action</Table.Th><Table.Th>Actor</Table.Th><Table.Th>Target</Table.Th></Table.Tr></Table.Thead>
              <Table.Tbody>{audit.map(entry => <Table.Tr key={entry.sequence}><Table.Td>{new Date(entry.created * 1000).toLocaleString()}</Table.Td><Table.Td>{entry.action}</Table.Td><Table.Td>{entry.actor}</Table.Td><Table.Td><Code className="key-value">{entry.target}</Code></Table.Td></Table.Tr>)}</Table.Tbody>
            </Table></Table.ScrollContainer>
            {moreAudit && <Button mt="md" variant="default" loading={busy} onClick={() => run(async () => {
              const rows = await api(`admin/audit?after=${audit.at(-1).sequence}`); setAudit([...audit, ...rows]); setMoreAudit(rows.length === 100);
            })}>Load more</Button>}
          </Tabs.Panel>
        </Tabs>
      </>}
    </main>
    <Modal opened={!!confirmation} onClose={() => setConfirmation(null)} title={confirmation?.kind === 'expel' ? 'Expel device' : 'Remove passkey'} centered>
      {confirmation && <Stack><Text>{confirmation.kind === 'expel' ? confirmation.value.name : confirmation.value.id}</Text>
        <Text size="sm">{confirmation.kind === 'expel' ? 'Existing data on this device will remain intact.' : 'All administrator sessions will be signed out.'}</Text>
        <Group justify="flex-end"><Button variant="default" onClick={() => setConfirmation(null)}>Cancel</Button><Button color="red" loading={busy} onClick={() => run(confirm)}>{confirmation.kind === 'expel' ? 'Expel' : 'Remove'}</Button></Group>
      </Stack>}
    </Modal>
  </MantineProvider>;
}

createRoot(document.getElementById('root')).render(<App />);
