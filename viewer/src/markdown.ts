import MarkdownIt from 'markdown-it';
import taskLists  from 'markdown-it-task-lists';
import footnote   from 'markdown-it-footnote';
import deflist    from 'markdown-it-deflist';
import attrs      from 'markdown-it-attrs';
import anchor     from 'markdown-it-anchor';

const md = new MarkdownIt({
    html:        false,
    linkify:     true,
    typographer: false,
    breaks:      false,
});

md.use(taskLists, { enabled: false });
md.use(footnote);
md.use(deflist);
md.use(attrs);
md.use(anchor, { permalink: false });

const defaultValidate = md.validateLink.bind(md);
md.validateLink = (url: string): boolean => {
    const lower = url.trim().toLowerCase();
    if (lower.startsWith('javascript:')) return false;
    if (lower.startsWith('data:'))       return false;
    return defaultValidate(url);
};

export function render(content: string): string {
    return md.render(content);
}
