import type { ButtonHTMLAttributes } from 'react';

// 通用按钮：默认带 ui-button 样式；className 追加。
export function Button({
  className = '',
  type = 'button',
  ...props
}: ButtonHTMLAttributes<HTMLButtonElement>) {
  return <button {...props} type={type} className={'ui-button ' + className} />;
}
