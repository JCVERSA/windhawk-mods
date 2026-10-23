// Tests for .github/pr_flow.cjs using Node's built-in test runner.
// Run: node --test tests/pr_flow.test.cjs   (no dependencies required)
const { test } = require('node:test');
const assert = require('node:assert/strict');

const flow = require('../.github/pr_flow.cjs');

function mockCore() {
  return { infos: [], info(msg) { this.infos.push(msg); } };
}

function mockGithub({ labels = [], files = [], pullRequest } = {}) {
  const calls = { addedLabels: [], removedLabels: [], comments: [], reactions: [] };
  const github = {
    paginate: async () => labels,
    rest: {
      issues: {
        listLabelsOnIssue: {},
        addLabels: async (args) => { calls.addedLabels.push(args.labels); },
        removeLabel: async (args) => { calls.removedLabels.push(args.name); },
        createComment: async (args) => { calls.comments.push(args.body); },
        listComments: async () => ({ data: [] }),
      },
      pulls: {
        get: async () => ({ data: pullRequest }),
        listFiles: async () => ({ data: files }),
      },
      reactions: {
        createForIssueComment: async (args) => { calls.reactions.push(args.content); },
      },
    },
  };
  return { github, calls };
}

test('opened pull request gets waiting-for-author and the welcome comment', async () => {
  const { github, calls } = mockGithub();
  const core = mockCore();
  const context = {
    repo: { owner: 'o', repo: 'r' },
    payload: {
      action: 'opened',
      pull_request: { number: 7 },
    },
  };

  await flow.handlePullRequestEvent({ github, context, core });

  assert.deepEqual(calls.addedLabels, [['waiting-for-author']]);
  assert.equal(calls.comments.length, 1);
  assert.match(calls.comments[0], /\/ai-review/);
});

test('closed pull request loses its flow labels and gets no welcome comment', async () => {
  const { github, calls } = mockGithub({
    labels: [{ name: 'waiting-for-reviewer' }, { name: 'unrelated' }],
  });
  const core = mockCore();
  const context = {
    repo: { owner: 'o', repo: 'r' },
    payload: {
      action: 'closed',
      pull_request: { number: 7 },
    },
  };

  await flow.handlePullRequestEvent({ github, context, core });

  assert.deepEqual(calls.removedLabels, ['waiting-for-reviewer']);
  assert.deepEqual(calls.addedLabels, []);
  assert.equal(calls.comments.length, 0);
});

test('/ai-review from the author labels the PR waiting-for-ai-review', async () => {
  const pullRequest = {
    number: 7,
    state: 'open',
    draft: false,
    head: { sha: 'a'.repeat(40) },
    labels: [{ name: 'waiting-for-author' }],
  };
  const { github, calls } = mockGithub({
    files: [{ filename: 'mods/test-mod.wh.cpp', status: 'added' }],
    pullRequest,
  });
  const core = mockCore();
  const context = {
    repo: { owner: 'o', repo: 'r' },
    serverUrl: 'https://github.com',
    runId: 1,
    payload: {
      action: 'created',
      comment: {
        id: 1,
        body: '/ai-review',
        user: { login: 'author', type: 'User' },
        author_association: 'NONE',
      },
      issue: {
        number: 7,
        state: 'open',
        user: { login: 'author' },
        pull_request: {},
      },
    },
  };

  await flow.handleComment({ github, context, core, botLogin: 'bot[bot]' });

  assert.deepEqual(calls.addedLabels, [['waiting-for-ai-review']]);
  assert.deepEqual(calls.reactions, ['eyes']);
});

test('/ai-review from an unrelated user is ignored with a -1 reaction', async () => {
  const pullRequest = {
    number: 7,
    state: 'open',
    draft: false,
    head: { sha: 'a'.repeat(40) },
    labels: [{ name: 'waiting-for-author' }],
  };
  const { github, calls } = mockGithub({ pullRequest });
  const core = mockCore();
  const context = {
    repo: { owner: 'o', repo: 'r' },
    serverUrl: 'https://github.com',
    runId: 1,
    payload: {
      action: 'created',
      comment: {
        id: 2,
        body: '/ai-review',
        user: { login: 'stranger', type: 'User' },
        author_association: 'NONE',
      },
      issue: {
        number: 7,
        state: 'open',
        user: { login: 'author' },
        pull_request: {},
      },
    },
  };

  await flow.handleComment({ github, context, core, botLogin: 'bot[bot]' });

  assert.deepEqual(calls.addedLabels, []);
  assert.deepEqual(calls.reactions, ['-1']);
});
