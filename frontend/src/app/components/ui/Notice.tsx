import type { ReactNode } from 'react';
import { useLocale } from '../../locales';
import { Button } from './Button';

export interface NoticeProps {
  children?: ReactNode;
  tone?: 'status' | 'error';
  onDismiss?: () => void;
  id?: string;
}

export function Notice({ children, tone = 'status', onDismiss, id }: NoticeProps) {
  const { t } = useLocale();
  return (
    <div
      id={id}
      className={'ui-notice' + (tone === 'error' ? ' page-error' : '')}
      role={tone === 'error' ? 'alert' : 'status'}
    >
      <span>{children}</span>
      {onDismiss && (
        <Button
          className="notice-dismiss"
          onClick={onDismiss}
          aria-label={t('notice.dismiss')}
          title={t('notice.dismiss')}
        >
          ×
        </Button>
      )}
    </div>
  );
}
